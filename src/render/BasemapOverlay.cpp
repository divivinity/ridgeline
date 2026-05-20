#include "BasemapOverlay.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

struct BaseVert { float x, y, z, u, v; };

static constexpr int kTileSize = 256;
static constexpr double kPi = 3.14159265358979323846;

BasemapOverlay::MercatorPixel
BasemapOverlay::mercatorPixel(double latDeg, double lonDeg, int zoom) {
    latDeg = std::clamp(latDeg, -85.05112878, 85.05112878);
    const double latRad = latDeg * kPi / 180.0;
    const double n = static_cast<double>(1 << zoom) * kTileSize;
    return {
        (lonDeg + 180.0) / 360.0 * n,
        (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / kPi) * 0.5 * n
    };
}

QString BasemapOverlay::tileUrlTemplate() {
    const char* env = std::getenv("RIDGELINE_TILE_URL");
    if (env && *env) return QString::fromUtf8(env);
    return QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
}

int BasemapOverlay::chooseZoom(const TerrainModel& terrain) {
    const auto sw = terrain.worldToLatLon(0.0f, 0.0f);
    const auto ne = terrain.worldToLatLon(terrain.worldSize().x, terrain.worldSize().z);
    const double spanDeg = std::max(std::abs(ne.latDeg - sw.latDeg),
                                    std::abs(ne.lonDeg - sw.lonDeg));
    if (spanDeg > 0.45) return 10;
    if (spanDeg > 0.22) return 11;
    if (spanDeg > 0.10) return 12;
    if (spanDeg > 0.05) return 13;
    return 14;
}

void BasemapOverlay::build(const TerrainModel& terrain) {
    initializeOpenGLFunctions();
    m_shader.load("assets/shaders/basemap.vert", "assets/shaders/basemap.frag");

    std::vector<BaseVert> verts;
    verts.reserve(terrain.width * terrain.height);
    for (int z = 0; z < terrain.height; ++z) {
        for (int x = 0; x < terrain.width; ++x) {
            const float wx = x * terrain.metersPerCellX();
            const float wz = z * terrain.metersPerCellZ();
            const float wy = terrain.heightAt(x, z);
            const float u = static_cast<float>(x) / (terrain.width - 1);
            const float v = static_cast<float>(z) / (terrain.height - 1);
            verts.push_back({wx, wy, wz, u, v});
        }
    }

    std::vector<unsigned int> idx;
    idx.reserve((terrain.width - 1) * (terrain.height - 1) * 6);
    for (int z = 0; z < terrain.height - 1; ++z) {
        for (int x = 0; x < terrain.width - 1; ++x) {
            unsigned int tl = z * terrain.width + x;
            unsigned int tr = tl + 1;
            unsigned int bl = tl + terrain.width;
            unsigned int br = bl + 1;
            idx.insert(idx.end(), {tl, bl, tr, tr, bl, br});
        }
    }
    m_indexCount = static_cast<int>(idx.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(BaseVert),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int),
                 idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BaseVert), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BaseVert), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    glGenTextures(1, &m_tex);
    m_hasTexture = loadTextureForTerrain(terrain);
}

bool BasemapOverlay::loadTextureForTerrain(const TerrainModel& terrain) {
    if (!terrain.hasGeoReference()) return false;

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
    if ((tileMaxX - tileMinX + 1) * (tileMaxY - tileMinY + 1) > 64)
        return false;

    QImage mosaic((tileMaxX - tileMinX + 1) * kTileSize,
                  (tileMaxY - tileMinY + 1) * kTileSize,
                  QImage::Format_RGBA8888);
    mosaic.fill(Qt::transparent);

    QNetworkAccessManager network;
    QPainter painter(&mosaic);
    const QString tmpl = tileUrlTemplate();
    for (int ty = tileMinY; ty <= tileMaxY; ++ty) {
        for (int tx = tileMinX; tx <= tileMaxX; ++tx) {
            QString url = tmpl;
            url.replace("{z}", QString::number(zoom));
            url.replace("{x}", QString::number(tx));
            url.replace("{y}", QString::number(ty));

            QNetworkRequest request{QUrl(url)};
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              "Ridgeline RF planner/1.0");
            QNetworkReply* reply = network.get(request);
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            QImage tile;
            if (reply->error() == QNetworkReply::NoError)
                tile.loadFromData(reply->readAll());
            reply->deleteLater();
            if (tile.isNull()) continue;

            painter.drawImage((tx - tileMinX) * kTileSize,
                              (ty - tileMinY) * kTileSize,
                              tile.convertToFormat(QImage::Format_RGBA8888));
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    painter.end();

    QRect crop(
        static_cast<int>(std::round(nwPx.x - tileMinX * kTileSize)),
        static_cast<int>(std::round(nwPx.y - tileMinY * kTileSize)),
        std::max(1, static_cast<int>(std::round(sePx.x - nwPx.x))),
        std::max(1, static_cast<int>(std::round(sePx.y - nwPx.y))));
    crop = crop.intersected(mosaic.rect());
    if (crop.isEmpty()) return false;

    QImage image = mosaic.copy(crop).mirrored(false, true)
        .scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return false;

    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void BasemapOverlay::draw(const glm::mat4& mvp) {
    if (!visible || !m_hasTexture || m_indexCount == 0) return;
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
