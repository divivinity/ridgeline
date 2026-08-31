#include "BasemapOverlay.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int kTileSize = 256;
constexpr double kPi = 3.14159265358979323846;

struct BaseVert { float x, y, z, u, v; };

QByteArray waitForReply(QNetworkReply* reply, QString& error) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(12000);
    loop.exec();

    QByteArray body;
    if (reply->error() == QNetworkReply::NoError) {
        body = reply->readAll();
    } else {
        error = reply->error() == QNetworkReply::OperationCanceledError
            ? QStringLiteral("request timed out") : reply->errorString();
    }
    reply->deleteLater();
    return body;
}

struct TileProvider {
    enum class Kind { Esri, Google, Custom };
    Kind kind = Kind::Esri;
    QString urlTemplate;
    QString apiKey;
    QString session;
    QString name;
    QString attribution;
};

bool createGoogleSession(QNetworkAccessManager& network,
                         const QString& apiKey,
                         TileProvider& provider,
                         QString& error) {
    QUrl url("https://tile.googleapis.com/v1/createSession");
    QUrlQuery query;
    query.addQueryItem("key", apiKey);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "Ridgeline-RF-Planner/1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    const QByteArray body = QJsonDocument(QJsonObject{
        {"mapType", "satellite"}, {"language", "en-US"}, {"region", "US"}
    }).toJson(QJsonDocument::Compact);
    const QByteArray response = waitForReply(network.post(request, body), error);
    const QJsonObject object = QJsonDocument::fromJson(response).object();
    provider.session = object.value("session").toString();
    if (provider.session.isEmpty()) {
        if (error.isEmpty()) error = object.value("error").toObject()
            .value("message").toString("Google Maps did not return a session token");
        return false;
    }
    provider.kind = TileProvider::Kind::Google;
    provider.apiKey = apiKey;
    provider.name = "Google Maps";
    provider.attribution = "Google Maps";
    return true;
}

QString googleTileUrl(const TileProvider& provider, int z, int x, int y) {
    QUrl url(QString("https://tile.googleapis.com/v1/2dtiles/%1/%2/%3")
             .arg(z).arg(x).arg(y));
    QUrlQuery query;
    query.addQueryItem("session", provider.session);
    query.addQueryItem("key", provider.apiKey);
    url.setQuery(query);
    return url.toString();
}
} // namespace

BasemapOverlay::MercatorPixel
BasemapOverlay::mercatorPixel(double latDeg, double lonDeg, int zoom) {
    latDeg = std::clamp(latDeg, -85.05112878, 85.05112878);
    const double latRad = latDeg * kPi / 180.0;
    const double n = std::ldexp(static_cast<double>(kTileSize), zoom);
    return {
        (lonDeg + 180.0) / 360.0 * n,
        (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / kPi) * 0.5 * n
    };
}

int BasemapOverlay::chooseZoom(const TerrainModel& terrain) {
    const auto sw = terrain.worldToLatLon(0.0f, 0.0f);
    const auto ne = terrain.worldToLatLon(terrain.worldSize().x, terrain.worldSize().z);
    const double minLat = std::min(sw.latDeg, ne.latDeg);
    const double maxLat = std::max(sw.latDeg, ne.latDeg);
    const double minLon = std::min(sw.lonDeg, ne.lonDeg);
    const double maxLon = std::max(sw.lonDeg, ne.lonDeg);
    for (int zoom = 17; zoom >= 5; --zoom) {
        const MercatorPixel nw = mercatorPixel(maxLat, minLon, zoom);
        const MercatorPixel se = mercatorPixel(minLat, maxLon, zoom);
        const int tilesX = static_cast<int>(std::floor(se.x / kTileSize))
                         - static_cast<int>(std::floor(nw.x / kTileSize)) + 1;
        const int tilesY = static_cast<int>(std::floor(se.y / kTileSize))
                         - static_cast<int>(std::floor(nw.y / kTileSize)) + 1;
        if (tilesX > 0 && tilesY > 0 && tilesX * tilesY <= 64 &&
            tilesX <= 8 && tilesY <= 8) return zoom;
    }
    return 5;
}

void BasemapOverlay::build(const TerrainModel& terrain) {
    initializeOpenGLFunctions();
    m_shader.load("assets/shaders/basemap.vert", "assets/shaders/basemap.frag");
    m_attribution.clear();
    m_providerName.clear();
    m_lastError.clear();

    MercatorPixel swPx{}, nePx{};
    if (terrain.hasGeoReference()) {
        const auto sw = terrain.worldToLatLon(0.0f, 0.0f);
        const auto ne = terrain.worldToLatLon(terrain.worldSize().x, terrain.worldSize().z);
        swPx = mercatorPixel(sw.latDeg, sw.lonDeg, 0);
        nePx = mercatorPixel(ne.latDeg, ne.lonDeg, 0);
    }

    std::vector<BaseVert> verts;
    verts.reserve(terrain.width * terrain.height);
    for (int z = 0; z < terrain.height; ++z) {
        for (int x = 0; x < terrain.width; ++x) {
            const float wx = x * terrain.metersPerCellX();
            const float wz = z * terrain.metersPerCellZ();
            const float wy = terrain.heightAt(x, z);
            float u = static_cast<float>(x) / std::max(1, terrain.width - 1);
            float v = static_cast<float>(z) / std::max(1, terrain.height - 1);
            if (terrain.hasGeoReference()) {
                const auto ll = terrain.worldToLatLon(wx, wz);
                const MercatorPixel p = mercatorPixel(ll.latDeg, ll.lonDeg, 0);
                if (std::abs(nePx.x - swPx.x) > 1e-12)
                    u = static_cast<float>((p.x - swPx.x) / (nePx.x - swPx.x));
                if (std::abs(swPx.y - nePx.y) > 1e-12)
                    v = static_cast<float>((swPx.y - p.y) / (swPx.y - nePx.y));
            }
            verts.push_back({wx, wy, wz, u, v});
        }
    }

    std::vector<unsigned int> idx;
    idx.reserve((terrain.width - 1) * (terrain.height - 1) * 6);
    for (int z = 0; z < terrain.height - 1; ++z) {
        for (int x = 0; x < terrain.width - 1; ++x) {
            const unsigned int tl = z * terrain.width + x;
            const unsigned int tr = tl + 1;
            const unsigned int bl = tl + terrain.width;
            const unsigned int br = bl + 1;
            idx.insert(idx.end(), {tl, bl, tr, tr, bl, br});
        }
    }
    m_indexCount = static_cast<int>(idx.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(BaseVert), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BaseVert), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BaseVert),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    glGenTextures(1, &m_tex);
    m_hasTexture = loadTextureForTerrain(terrain);
}

bool BasemapOverlay::loadTextureForTerrain(const TerrainModel& terrain) {
    if (!terrain.hasGeoReference()) {
        m_lastError = "Terrain is not georeferenced";
        return false;
    }

    const auto sw = terrain.worldToLatLon(0.0f, 0.0f);
    const auto ne = terrain.worldToLatLon(terrain.worldSize().x, terrain.worldSize().z);
    const double minLat = std::min(sw.latDeg, ne.latDeg);
    const double maxLat = std::max(sw.latDeg, ne.latDeg);
    const double minLon = std::min(sw.lonDeg, ne.lonDeg);
    const double maxLon = std::max(sw.lonDeg, ne.lonDeg);
    const int zoom = chooseZoom(terrain);
    const MercatorPixel nwPx = mercatorPixel(maxLat, minLon, zoom);
    const MercatorPixel sePx = mercatorPixel(minLat, maxLon, zoom);
    const int tileMinX = static_cast<int>(std::floor(nwPx.x / kTileSize));
    const int tileMaxX = static_cast<int>(std::floor(sePx.x / kTileSize));
    const int tileMinY = static_cast<int>(std::floor(nwPx.y / kTileSize));
    const int tileMaxY = static_cast<int>(std::floor(sePx.y / kTileSize));
    if (tileMaxX < tileMinX || tileMaxY < tileMinY) return false;

    QNetworkAccessManager network;
    TileProvider provider;
    const QString customTemplate = qEnvironmentVariable("RIDGELINE_TILE_URL");
    if (!customTemplate.isEmpty()) {
        provider.kind = TileProvider::Kind::Custom;
        provider.urlTemplate = customTemplate;
        provider.name = "Custom imagery";
        provider.attribution = qEnvironmentVariable("RIDGELINE_TILE_ATTRIBUTION",
                                                     "Custom imagery");
    } else {
        const QString apiKey = qEnvironmentVariable("RIDGELINE_GOOGLE_MAPS_API_KEY");
        const QString requested = qEnvironmentVariable("RIDGELINE_BASEMAP_PROVIDER").toLower();
        if (!apiKey.isEmpty() && requested != "esri") {
            QString googleError;
            if (!createGoogleSession(network, apiKey, provider, googleError))
                m_lastError = "Google Maps unavailable: " + googleError + "; using Esri imagery";
        }
        if (provider.kind != TileProvider::Kind::Google) {
            provider.kind = TileProvider::Kind::Esri;
            provider.urlTemplate = QStringLiteral(
                "https://server.arcgisonline.com/ArcGIS/rest/services/"
                "World_Imagery/MapServer/tile/{z}/{y}/{x}");
            provider.name = "Esri World Imagery";
            provider.attribution =
                "Imagery © Esri, Maxar, Earthstar Geographics, and contributors";
        }
    }

    if (provider.kind == TileProvider::Kind::Google) {
        QUrl viewportUrl("https://tile.googleapis.com/tile/v1/viewport");
        QUrlQuery query;
        query.addQueryItem("session", provider.session);
        query.addQueryItem("key", provider.apiKey);
        query.addQueryItem("zoom", QString::number(zoom));
        query.addQueryItem("north", QString::number(maxLat, 'f', 8));
        query.addQueryItem("south", QString::number(minLat, 'f', 8));
        query.addQueryItem("east", QString::number(maxLon, 'f', 8));
        query.addQueryItem("west", QString::number(minLon, 'f', 8));
        viewportUrl.setQuery(query);
        QNetworkRequest request(viewportUrl);
        request.setHeader(QNetworkRequest::UserAgentHeader, "Ridgeline-RF-Planner/1.0");
        QString error;
        const QJsonObject metadata = QJsonDocument::fromJson(
            waitForReply(network.get(request), error)).object();
        const QString copyright = metadata.value("copyright").toString();
        if (!copyright.isEmpty()) provider.attribution = "Google Maps · " + copyright;
        else {
            m_lastError = error.isEmpty()
                ? QStringLiteral("Google Maps attribution metadata was unavailable; using Esri imagery")
                : QStringLiteral("Google Maps attribution lookup failed: %1; using Esri imagery").arg(error);
            provider = TileProvider{};
            provider.kind = TileProvider::Kind::Esri;
            provider.urlTemplate = QStringLiteral(
                "https://server.arcgisonline.com/ArcGIS/rest/services/"
                "World_Imagery/MapServer/tile/{z}/{y}/{x}");
            provider.name = "Esri World Imagery";
            provider.attribution =
                "Imagery © Esri, Maxar, Earthstar Geographics, and contributors";
        }
    }

    QImage mosaic((tileMaxX - tileMinX + 1) * kTileSize,
                  (tileMaxY - tileMinY + 1) * kTileSize,
                  QImage::Format_RGBA8888);
    mosaic.fill(Qt::transparent);

    int loaded = 0;
    int failed = 0;
    for (int ty = tileMinY; ty <= tileMaxY; ++ty) {
        for (int tx = tileMinX; tx <= tileMaxX; ++tx) {
            QString url;
            if (provider.kind == TileProvider::Kind::Google) {
                url = googleTileUrl(provider, zoom, tx, ty);
            } else {
                url = provider.urlTemplate;
                url.replace("{z}", QString::number(zoom));
                url.replace("{x}", QString::number(tx));
                url.replace("{y}", QString::number(ty));
            }
            QNetworkRequest request{QUrl(url)};
            request.setHeader(QNetworkRequest::UserAgentHeader, "Ridgeline-RF-Planner/1.0");
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
            QString error;
            QImage tile;
            tile.loadFromData(waitForReply(network.get(request), error));
            if (tile.isNull()) {
                ++failed;
                if (m_lastError.isEmpty()) m_lastError = "Imagery tile failed: " + error;
                continue;
            }
            QPainter painter(&mosaic);
            painter.drawImage((tx - tileMinX) * kTileSize,
                              (ty - tileMinY) * kTileSize,
                              tile.convertToFormat(QImage::Format_RGBA8888));
            ++loaded;
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    if (loaded == 0) return false;
    if (failed > 0)
        m_lastError = QString("%1 imagery tile(s) could not be loaded").arg(failed);

    const QRectF sourceRect(nwPx.x - tileMinX * kTileSize,
                            nwPx.y - tileMinY * kTileSize,
                            sePx.x - nwPx.x,
                            sePx.y - nwPx.y);
    const int targetW = std::clamp(static_cast<int>(std::ceil(sourceRect.width())), 1, 2048);
    const int targetH = std::clamp(static_cast<int>(std::ceil(sourceRect.height())), 1, 2048);
    QImage image(targetW, targetH, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(0, 0, targetW, targetH), mosaic, sourceRect);
    }
    image = image.mirrored(false, true);
    if (image.isNull()) return false;

    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_providerName = provider.name;
    m_attribution = provider.attribution;
    return true;
}

void BasemapOverlay::setOpacity(float opacity) {
    m_opacity = std::clamp(opacity, 0.05f, 1.0f);
}

void BasemapOverlay::draw(const glm::mat4& mvp) {
    if (!visible || !m_hasTexture || m_indexCount == 0) return;
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_shader.bind();
    m_shader.setMat4("uMVP", mvp);
    m_shader.setFloat("uOpacity", m_opacity);
    m_shader.setInt("uBasemap", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    m_shader.release();
    glDisable(GL_BLEND);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void BasemapOverlay::destroy() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
    glDeleteTextures(1, &m_tex);
    m_vao = m_vbo = m_ebo = m_tex = 0;
    m_indexCount = 0;
    m_hasTexture = false;
}
