#include "BasemapOverlay.h"
#include "data/TileBatchDownloader.h"

#include <QEventLoop>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QOpenGLContext>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {
constexpr int kTileSize = 256;
constexpr double kPi = 3.14159265358979323846;

struct BaseVert { float x, y, z, u, v; };

double normalizeLongitude(double lonDeg) {
    double wrapped = std::fmod(lonDeg + 180.0, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return wrapped - 180.0;
}

int wrapTileX(int tileX, int zoom) {
    const int tileCount = 1 << zoom;
    int wrapped = tileX % tileCount;
    if (wrapped < 0) wrapped += tileCount;
    return wrapped;
}

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
    QString cacheKey = "esri";
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
    request.setTransferTimeout(12000);
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
    provider.cacheKey = "google";
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

void configureEsri(TileProvider& provider) {
    provider = TileProvider{};
    provider.kind = TileProvider::Kind::Esri;
    provider.urlTemplate = QStringLiteral(
        "https://server.arcgisonline.com/ArcGIS/rest/services/"
        "World_Imagery/MapServer/tile/{z}/{y}/{x}");
    provider.name = "Esri World Imagery";
    provider.attribution =
        "Imagery © Esri, Maxar, Earthstar Geographics, and contributors";
    provider.cacheKey = "esri";
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
    const double south = terrain.originLatDeg;
    const double north = terrain.originLatDeg
                       + static_cast<double>(terrain.height - 1) * terrain.cellLatDeg;
    const double west = terrain.originLonDeg;
    const double east = terrain.originLonDeg
                      + static_cast<double>(terrain.width - 1) * terrain.cellLonDeg;
    const double minLat = std::min(south, north);
    const double maxLat = std::max(south, north);
    const double minLon = std::min(west, east);
    const double maxLon = std::max(west, east);
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
    m_attribution.clear();
    m_providerName.clear();
    m_lastError.clear();
    const bool shaderReady = m_shader.load(
        "assets/shaders/basemap.vert", "assets/shaders/basemap.frag");

    MercatorPixel swPx{}, nePx{};
    if (terrain.hasGeoReference()) {
        const double south = terrain.originLatDeg;
        const double north = terrain.originLatDeg
                           + static_cast<double>(terrain.height - 1) * terrain.cellLatDeg;
        const double west = terrain.originLonDeg;
        const double east = terrain.originLonDeg
                          + static_cast<double>(terrain.width - 1) * terrain.cellLonDeg;
        swPx = mercatorPixel(south, west, 0);
        nePx = mercatorPixel(north, east, 0);
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
                const double lat = terrain.originLatDeg
                                 + static_cast<double>(z) * terrain.cellLatDeg;
                const double lon = terrain.originLonDeg
                                 + static_cast<double>(x) * terrain.cellLonDeg;
                const MercatorPixel p = mercatorPixel(lat, lon, 0);
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
    if (!shaderReady) {
        m_lastError =
            "Satellite imagery could not start because its rendering assets are missing";
        m_hasTexture = false;
    } else {
        m_hasTexture = loadTextureForTerrain(terrain);
    }
}

bool BasemapOverlay::loadTextureForTerrain(const TerrainModel& terrain) {
    if (!terrain.hasGeoReference()) {
        m_lastError = "Terrain is not georeferenced";
        return false;
    }

    QOpenGLContext* const expectedContext = QOpenGLContext::currentContext();
    const double south = terrain.originLatDeg;
    const double north = terrain.originLatDeg
                       + static_cast<double>(terrain.height - 1) * terrain.cellLatDeg;
    const double west = terrain.originLonDeg;
    const double east = terrain.originLonDeg
                      + static_cast<double>(terrain.width - 1) * terrain.cellLonDeg;
    const double minLat = std::min(south, north);
    const double maxLat = std::max(south, north);
    const double minLon = std::min(west, east);
    const double maxLon = std::max(west, east);
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
        provider.cacheKey = "custom";
    } else {
        const QString apiKey = qEnvironmentVariable("RIDGELINE_GOOGLE_MAPS_API_KEY");
        const QString requested = qEnvironmentVariable("RIDGELINE_BASEMAP_PROVIDER").toLower();
        if (!apiKey.isEmpty() && requested != "esri") {
            if (TileBatchDownloader::offlineMode()) {
                provider.kind = TileProvider::Kind::Google;
                provider.apiKey = apiKey;
                provider.name = "Google Maps (offline cache)";
                provider.attribution = "Google Maps";
                provider.cacheKey = "google";
            } else {
                QString googleError;
                if (!createGoogleSession(network, apiKey, provider, googleError))
                    m_lastError = "Google Maps unavailable: " + googleError + "; using Esri imagery";
            }
        }
        if (provider.kind != TileProvider::Kind::Google)
            configureEsri(provider);
    }

    if (provider.kind == TileProvider::Kind::Google &&
        !TileBatchDownloader::offlineMode()) {
        QUrl viewportUrl("https://tile.googleapis.com/tile/v1/viewport");
        QUrlQuery query;
        query.addQueryItem("session", provider.session);
        query.addQueryItem("key", provider.apiKey);
        query.addQueryItem("zoom", QString::number(zoom));
        query.addQueryItem("north", QString::number(maxLat, 'f', 8));
        query.addQueryItem("south", QString::number(minLat, 'f', 8));
        query.addQueryItem("east", QString::number(normalizeLongitude(maxLon), 'f', 8));
        query.addQueryItem("west", QString::number(normalizeLongitude(minLon), 'f', 8));
        viewportUrl.setQuery(query);
        QNetworkRequest request(viewportUrl);
        request.setHeader(QNetworkRequest::UserAgentHeader, "Ridgeline-RF-Planner/1.0");
        request.setTransferTimeout(12000);
        QString error;
        const QJsonObject metadata = QJsonDocument::fromJson(
            waitForReply(network.get(request), error)).object();
        const QString copyright = metadata.value("copyright").toString();
        if (!copyright.isEmpty()) provider.attribution = "Google Maps · " + copyright;
        else {
            m_lastError = error.isEmpty()
                ? QStringLiteral("Google Maps attribution metadata was unavailable; using Esri imagery")
                : QStringLiteral("Google Maps attribution lookup failed: %1; using Esri imagery").arg(error);
            configureEsri(provider);
        }
    }

    QImage mosaic((tileMaxX - tileMinX + 1) * kTileSize,
                  (tileMaxY - tileMinY + 1) * kTileSize,
                  QImage::Format_RGBA8888);
    mosaic.fill(Qt::transparent);

    std::vector<TileDownloadRequest> requests;
    std::vector<std::pair<int, int>> tileCoords;
    const int expected = (tileMaxX - tileMinX + 1) * (tileMaxY - tileMinY + 1);
    requests.reserve(expected);
    tileCoords.reserve(expected);

    for (int ty = tileMinY; ty <= tileMaxY; ++ty) {
        for (int tx = tileMinX; tx <= tileMaxX; ++tx) {
            const int requestX = wrapTileX(tx, zoom);
            QString url;
            if (provider.kind == TileProvider::Kind::Google) {
                url = googleTileUrl(provider, zoom, requestX, ty);
            } else {
                url = provider.urlTemplate;
                url.replace("{z}", QString::number(zoom));
                url.replace("{x}", QString::number(requestX));
                url.replace("{y}", QString::number(ty));
            }
            requests.push_back({
                QUrl(url),
                QStringLiteral("imagery/%1/%2/%3/%4.tile")
                    .arg(provider.cacheKey).arg(zoom).arg(requestX).arg(ty)
            });
            tileCoords.emplace_back(tx, ty);
        }
    }

    const auto results = TileBatchDownloader::download(
        network, requests, 8, 12000, m_loadProgress, m_loadCancel);
    // The nested network loop and a modal QProgressDialog may process paint
    // events, changing the QOpenGLWidget context. Reassert the owning widget's
    // context before any texture upload or subsequent overlay construction.
    if (m_prepareOpenGL) m_prepareOpenGL();
    if (!expectedContext || QOpenGLContext::currentContext() != expectedContext) {
        m_lastError = "Could not restore the map's OpenGL context for imagery upload";
        return false;
    }
    if (m_loadCancel && m_loadCancel()) {
        m_lastError = "Basemap loading cancelled";
        return false;
    }

    int loaded = 0;
    int failed = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        QImage tile;
        tile.loadFromData(results[i].bytes);
        if (tile.isNull()) {
            ++failed;
            if (m_lastError.isEmpty()) {
                m_lastError = results[i].error.isEmpty()
                    ? QStringLiteral("Imagery tile was invalid")
                    : QStringLiteral("Imagery tile failed: %1").arg(results[i].error);
            }
            continue;
        }
        const int tx = tileCoords[i].first;
        const int ty = tileCoords[i].second;
        QPainter painter(&mosaic);
        painter.drawImage((tx - tileMinX) * kTileSize,
                          (ty - tileMinY) * kTileSize,
                          tile.convertToFormat(QImage::Format_RGBA8888));
        ++loaded;
    }
    if (loaded == 0) {
        if (TileBatchDownloader::offlineMode() && m_lastError.isEmpty())
            m_lastError = QString("Offline imagery cache has no tiles for this area: %1")
                .arg(TileBatchDownloader::cacheRoot());
        return false;
    }
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

    if (m_prepareOpenGL) m_prepareOpenGL();
    if (QOpenGLContext::currentContext() != expectedContext) {
        m_lastError = "Could not make the map's OpenGL context current for imagery upload";
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {}
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    const GLenum uploadError = glGetError();
    if (uploadError != GL_NO_ERROR) {
        m_lastError = QString("Satellite texture upload failed (OpenGL error 0x%1)")
            .arg(static_cast<unsigned int>(uploadError), 0, 16);
        return false;
    }
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
    m_shader.destroy();
    m_vao = m_vbo = m_ebo = m_tex = 0;
    m_indexCount = 0;
    m_hasTexture = false;
}
