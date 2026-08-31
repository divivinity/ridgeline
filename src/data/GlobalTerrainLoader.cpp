#include "GlobalTerrainLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace {
constexpr int kTileSize = 256;
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxMercatorLat = 85.05112878;

struct PixelPoint { double x = 0.0; double y = 0.0; };

PixelPoint mercatorPixel(double latDeg, double lonDeg, int zoom) {
    latDeg = std::clamp(latDeg, -kMaxMercatorLat, kMaxMercatorLat);
    const double latRad = latDeg * kPi / 180.0;
    const double worldPixels = std::ldexp(static_cast<double>(kTileSize), zoom);
    return {
        (lonDeg + 180.0) / 360.0 * worldPixels,
        (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / kPi)
            * 0.5 * worldPixels
    };
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
    timeout.start(15000);
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

double decodeTerrarium(QRgb pixel) {
    return static_cast<double>(qRed(pixel)) * 256.0
         + static_cast<double>(qGreen(pixel))
         + static_cast<double>(qBlue(pixel)) / 256.0
         - 32768.0;
}

double bilinearHeight(const QImage& mosaic, double x, double y) {
    x = std::clamp(x, 0.0, static_cast<double>(mosaic.width() - 1));
    y = std::clamp(y, 0.0, static_cast<double>(mosaic.height() - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, mosaic.width() - 1);
    const int y1 = std::min(y0 + 1, mosaic.height() - 1);
    const double tx = x - x0;
    const double ty = y - y0;
    const double h00 = decodeTerrarium(mosaic.pixel(x0, y0));
    const double h10 = decodeTerrarium(mosaic.pixel(x1, y0));
    const double h01 = decodeTerrarium(mosaic.pixel(x0, y1));
    const double h11 = decodeTerrarium(mosaic.pixel(x1, y1));
    return h00 * (1.0 - tx) * (1.0 - ty)
         + h10 * tx * (1.0 - ty)
         + h01 * (1.0 - tx) * ty
         + h11 * tx * ty;
}
} // namespace

TerrainModel GlobalTerrainLoader::load(double centerLat,
                                       double centerLon,
                                       double extentKm,
                                       int samplesPerAxis) {
    if (!std::isfinite(centerLat) || !std::isfinite(centerLon) ||
        centerLat <= -kMaxMercatorLat || centerLat >= kMaxMercatorLat ||
        centerLon < -180.0 || centerLon > 180.0 ||
        !std::isfinite(extentKm) || extentKm < 1.0 || extentKm > 100.0) {
        throw std::invalid_argument(
            "Global terrain requires a valid Web Mercator location and a 1-100 km area");
    }
    samplesPerAxis = std::clamp(samplesPerAxis, 129, 769);

    const double extentM = extentKm * 1000.0;
    const double halfM = extentM * 0.5;
    const double halfLatDeg = halfM / TerrainModel::metersPerDegreeLat(centerLat);
    const double halfLonDeg = halfM / TerrainModel::metersPerDegreeLon(centerLat);
    const double south = std::max(-kMaxMercatorLat, centerLat - halfLatDeg);
    const double north = std::min( kMaxMercatorLat, centerLat + halfLatDeg);

    // Keep longitude bounds unwrapped. Web Mercator X is periodic, so a small
    // analysis area centered near +/-180 degrees should continue smoothly past
    // the date line instead of being truncated at the map edge.
    const double west = centerLon - halfLonDeg;
    const double east = centerLon + halfLonDeg;
    if (east <= west || north <= south)
        throw std::runtime_error("Selected area has invalid geographic bounds");

    const double desiredMpp = std::max(8.0, extentM / (samplesPerAxis - 1));
    const double circumference = 40075016.686 * std::cos(centerLat * kPi / 180.0);
    int zoom = static_cast<int>(std::ceil(
        std::log2(circumference / (desiredMpp * kTileSize))));
    zoom = std::clamp(zoom, 5, 14);

    PixelPoint nw = mercatorPixel(north, west, zoom);
    PixelPoint se = mercatorPixel(south, east, zoom);
    int tileMinX = static_cast<int>(std::floor(nw.x / kTileSize));
    int tileMaxX = static_cast<int>(std::floor(se.x / kTileSize));
    int tileMinY = static_cast<int>(std::floor(nw.y / kTileSize));
    int tileMaxY = static_cast<int>(std::floor(se.y / kTileSize));
    while ((tileMaxX - tileMinX + 1) * (tileMaxY - tileMinY + 1) > 64 && zoom > 5) {
        --zoom;
        nw = mercatorPixel(north, west, zoom);
        se = mercatorPixel(south, east, zoom);
        tileMinX = static_cast<int>(std::floor(nw.x / kTileSize));
        tileMaxX = static_cast<int>(std::floor(se.x / kTileSize));
        tileMinY = static_cast<int>(std::floor(nw.y / kTileSize));
        tileMaxY = static_cast<int>(std::floor(se.y / kTileSize));
    }

    QNetworkAccessManager network;
    auto* cache = new QNetworkDiskCache(&network);
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            + QStringLiteral("/terrain");
    QDir().mkpath(cacheRoot);
    cache->setCacheDirectory(cacheRoot);
    cache->setMaximumCacheSize(256LL * 1024LL * 1024LL);
    network.setCache(cache);

    QImage mosaic((tileMaxX - tileMinX + 1) * kTileSize,
                  (tileMaxY - tileMinY + 1) * kTileSize,
                  QImage::Format_ARGB32);
    mosaic.fill(Qt::transparent);

    QString urlTemplate = qEnvironmentVariable("RIDGELINE_TERRAIN_URL");
    if (urlTemplate.isEmpty()) {
        urlTemplate = QStringLiteral(
            "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png");
    }

    int loadedTiles = 0;
    QString lastError;
    for (int ty = tileMinY; ty <= tileMaxY; ++ty) {
        for (int tx = tileMinX; tx <= tileMaxX; ++tx) {
            const int requestX = wrapTileX(tx, zoom);
            QString url = urlTemplate;
            url.replace("{z}", QString::number(zoom));
            url.replace("{x}", QString::number(requestX));
            url.replace("{y}", QString::number(ty));
            QNetworkRequest request{QUrl(url)};
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              "Ridgeline-RF-Planner/1.0");
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
            QString error;
            const QByteArray bytes = waitForReply(network.get(request), error);
            QImage tile;
            tile.loadFromData(bytes);
            if (tile.isNull()) {
                lastError = error.isEmpty() ? QStringLiteral("invalid terrain tile") : error;
                continue;
            }
            tile = tile.convertToFormat(QImage::Format_ARGB32);
            for (int row = 0; row < kTileSize; ++row) {
                std::copy_n(reinterpret_cast<const QRgb*>(tile.constScanLine(row)),
                            kTileSize,
                            reinterpret_cast<QRgb*>(mosaic.scanLine(
                                (ty - tileMinY) * kTileSize + row))
                                + (tx - tileMinX) * kTileSize);
            }
            ++loadedTiles;
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    const int expectedTiles = (tileMaxX - tileMinX + 1) * (tileMaxY - tileMinY + 1);
    if (loadedTiles != expectedTiles) {
        throw std::runtime_error(QString(
            "Could not load global elevation data (%1 of %2 tiles; %3)")
            .arg(loadedTiles).arg(expectedTiles).arg(lastError).toStdString());
    }

    TerrainModel terrain;
    terrain.width = samplesPerAxis;
    terrain.height = samplesPerAxis;
    const double cellLatDeg = (north - south) / (samplesPerAxis - 1);
    const double cellLonDeg = (east - west) / (samplesPerAxis - 1);
    terrain.setCellSpacing(
        static_cast<float>(TerrainModel::metersPerDegreeLon(centerLat) * cellLonDeg),
        static_cast<float>(TerrainModel::metersPerDegreeLat(centerLat) * cellLatDeg));
    terrain.setGeoReference(south, west, cellLatDeg, cellLonDeg);
    terrain.heights.resize(samplesPerAxis * samplesPerAxis);

    const double mosaicOriginX = tileMinX * kTileSize;
    const double mosaicOriginY = tileMinY * kTileSize;
    for (int z = 0; z < samplesPerAxis; ++z) {
        const double lat = south + z * cellLatDeg;
        for (int x = 0; x < samplesPerAxis; ++x) {
            const double lon = west + x * cellLonDeg;
            const PixelPoint p = mercatorPixel(lat, lon, zoom);
            const double h = bilinearHeight(mosaic,
                                            p.x - mosaicOriginX,
                                            p.y - mosaicOriginY);
            terrain.heights[z * samplesPerAxis + x] =
                static_cast<float>(std::isfinite(h) ? h : 0.0);
        }
    }
    return terrain;
}
