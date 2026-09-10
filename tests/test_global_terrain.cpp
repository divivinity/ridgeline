#include "data/GlobalTerrainLoader.h"
#include "data/TileBatchDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;
void check(bool passed, const char* message) {
    if (!passed) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

// A 1 km area centered on (0,0), sampled at 129 points, uses the four
// zoom-15 tiles adjoining the equator/prime meridian. Terrarium is served to
// zoom 15 and 404s above it, which is why the loader caps there. Offline
// fixtures exercise downloading, decoding, mosaic alignment and resampling
// together.
QString tilePath(const QTemporaryDir& dir, int x, int y,
                 const QString& prefix = "terrain") {
    return dir.filePath(QString("%1/15/%2/%3.png").arg(prefix).arg(x).arg(y));
}
void fillTiles(const QTemporaryDir& dir, int size = 256, bool opaque = true,
               const QString& prefix = "terrain", int offset = 0) {
    for (int x : {16383, 16384}) {
        const QString folder = dir.filePath(QString("%1/15/%2").arg(prefix).arg(x));
        check(QDir().mkpath(folder), "create cache fixture");
        for (int y : {16383, 16384}) {
            QImage tile(size, size, QImage::Format_ARGB32);
            // Northern tiles are 200 m; southern tiles are 100 m.
            tile.fill(qRgba(128, (y == 16383 ? 200 : 100) + offset, 0, opaque ? 255 : 0));
            check(tile.save(tilePath(dir, x, y, prefix), "PNG"), "save cache fixture");
        }
    }
}
void rejects(const char* reason) {
    bool rejected = false;
    try { GlobalTerrainLoader::load(0, 0, 1, 129); }
    catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find(reason) != std::string::npos;
        if (!rejected) std::cerr << "Unexpected error: " << e.what() << '\n';
    }
    check(rejected, reason);
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    qputenv("RIDGELINE_CACHE_DIR", dir.path().toUtf8());
    qputenv("RIDGELINE_OFFLINE", "1");
    qunsetenv("RIDGELINE_TERRAIN_URL");
    fillTiles(dir);
    try {
        const auto terrain = GlobalTerrainLoader::load(0, 0, 1, 129);
        check(terrain.width == 129 && terrain.height == 129, "terrain dimensions");
        check(terrain.hasGeoReference(), "georeferenced terrain");
        check(terrain.elevationProvider == TerrainModel::ElevationProvider::Mapzen,
              "default load records its configured elevation provider");
        check(std::abs(terrain.heightAt(64, 0) - 100) < .01f, "southern elevation");
        check(std::abs(terrain.heightAt(64, 128) - 200) < .01f, "northern elevation");
        const auto center = terrain.worldToLatLon(terrain.worldSize().x / 2,
                                                  terrain.worldSize().z / 2);
        check(std::abs(center.latDeg) < 1e-6 && std::abs(center.lonDeg) < 1e-6,
              "terrain remains centered at requested coordinates");
    } catch (const std::exception& e) {
        std::cerr << "FAIL: valid fixture: " << e.what() << '\n'; ++failures;
    }
    const QString providerA = "https://invalid.example/terrain/{z}/{x}/{y}.png?dataset=A";
    const QString providerB = "https://invalid.example/terrain/{z}/{x}/{y}.png?dataset=B";
    qputenv("RIDGELINE_TERRAIN_URL", providerA.toUtf8());
    rejects("offline"); // No fallback to populated legacy/default tiles.
    fillTiles(dir, 256, true,
              "terrain/" + TileBatchDownloader::customProviderCacheKey(providerA), 30);
    try {
        const auto custom = GlobalTerrainLoader::load(0, 0, 1, 129);
        check(custom.elevationProvider == TerrainModel::ElevationProvider::Custom,
              "custom load records its own provider class");
        check(std::abs(custom.heightAt(64, 0) - 130) < .01f,
              "custom provider loads its own cached elevation");
        qputenv("RIDGELINE_TERRAIN_URL", providerB.toUtf8());
        rejects("offline"); // Dataset/query changes cannot reuse provider A.
        qunsetenv("RIDGELINE_TERRAIN_URL");
        check(custom.elevationProvider == TerrainModel::ElevationProvider::Custom,
              "loaded provenance survives runtime configuration changes");
        const auto original = GlobalTerrainLoader::load(0, 0, 1, 129);
        check(std::abs(original.heightAt(64, 0) - 100) < .01f,
              "switching back preserves the default portable cache");
    } catch (const std::exception& e) {
        std::cerr << "FAIL: provider isolation: " << e.what() << '\n'; ++failures;
    }
    qunsetenv("RIDGELINE_TERRAIN_URL");
    fillTiles(dir, 16);
    rejects("256 x 256");
    fillTiles(dir, 512);
    rejects("256 x 256");
    fillTiles(dir, 256, false);
    rejects("missing elevation");
    fillTiles(dir);
    check(QFile::remove(tilePath(dir, 16383, 16383)), "remove one fixture tile");
    rejects("offline");
    fillTiles(dir);
    QFile corrupt(tilePath(dir, 16383, 16383));
    check(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate), "open corrupt fixture");
    corrupt.write("not an image");
    corrupt.close();
    rejects("invalid terrain tile");
    fillTiles(dir);
    bool cancelled = false;
    try { GlobalTerrainLoader::load(0, 0, 1, 129, {}, [] { return true; }); }
    catch (const std::runtime_error& e) {
        cancelled = std::string(e.what()) == "Terrain load cancelled";
    }
    check(cancelled, "cached terrain load can be cancelled");
    if (!failures) std::cout << "Global terrain validation tests passed.\n";
    return failures ? 1 : 0;
}
