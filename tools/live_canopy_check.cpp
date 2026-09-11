// End-to-end check against the live LANDFIRE service: build a georeferenced
// terrain grid, fetch canopy for exactly its footprint, install it, and measure
// the loss along a path. Not part of the suite - it needs the network.
#include "data/CanopyLoader.h"
#include "map/CanopyShade.h"
#include "data/TerrainLoader.h"
#include "sim/RFModels.h"
#include "sim/SimEngine.h"

#include <QCoreApplication>
#include <QImage>
#include <QNetworkAccessManager>

#include <cstdio>
#include <cmath>
#include <map>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const double lat = argc > 1 ? std::atof(argv[1]) : 35.05;
    const double lon = argc > 2 ? std::atof(argv[2]) : -78.88;
    const double spanDeg = 0.08;
    const int n = 256;

    TerrainModel terrain = TerrainLoader::makeFlat(n, n, 30.0f, 50.0f);
    terrain.setGeoReference(lat - spanDeg * 0.5, lon - spanDeg * 0.5,
                            spanDeg / (n - 1), spanDeg / (n - 1));
    std::printf("terrain %dx%d at %.4f,%.4f span %.3f deg\n",
                terrain.width, terrain.height, lat, lon, spanDeg);

    SimEngine engine;
    engine.setTerrain(terrain);

    QNetworkAccessManager network;
    const CanopyLoader::Result result = CanopyLoader::loadForTerrain(
        network, engine.terrain(), {},
        [](int done, int total) { std::printf("  layer %d/%d\n", done, total); });

    if (!result.ok()) {
        std::printf("FAILED: %s\n", result.error.toUtf8().constData());
        return 1;
    }

    const CanopyRaster& canopy = result.canopy;
    std::printf("source   : %s (%s)\n",
                canopy.provenance.displayName.c_str(),
                canopy.provenance.productVintage.c_str());
    std::printf("licence  : %s\n", canopy.provenance.licence.c_str());
    std::printf("raster   : %dx%d, NW %.5f,%.5f, step %.6f\n",
                canopy.width, canopy.height,
                canopy.northLatDeg, canopy.westLonDeg, canopy.latStepDeg);
    std::printf("coverage : %d samples, %d with data, %d vegetated, %d assigned\n",
                canopy.coverage.samples, canopy.coverage.withData,
                canopy.coverage.vegetated, canopy.coverage.assignedHeight);
    if (!result.note.isEmpty())
        std::printf("note     : %s\n", result.note.toUtf8().constData());

    auto shared = std::make_shared<CanopyRaster>(canopy);
    CanopySourceInfo info;
    info.displayName = canopy.provenance.displayName;
    info.vegetatedSamples = canopy.coverage.vegetated;
    info.assignedSamples = canopy.coverage.assignedHeight;
    engine.setImportedCanopy(
        [shared](const TerrainModel& t, std::vector<std::uint16_t>& h,
                 std::vector<std::uint8_t>& c) { return shared->sampleOnto(t, h, c); },
        info);

    const TerrainModel& live = engine.terrain();
    std::printf("installed: hasVegetation=%d heightCells=%zu coverCells=%zu\n",
                live.hasVegetation() ? 1 : 0,
                live.canopyHeightDm.size(), live.canopyCoverPct.size());

    int vegetated = 0, tallest = 0;
    long long sum = 0;
    for (std::uint16_t v : live.canopyHeightDm) {
        if (v > 0) { ++vegetated; sum += v; }
        if (v > tallest) tallest = v;
    }
    std::map<int,int> histogram;
    for (std::uint16_t v : live.canopyHeightDm) if (v > 0) ++histogram[v];
    std::printf("heights  :");
    for (const auto& entry : histogram)
        std::printf(" %.1fm=%d", entry.first * 0.1, entry.second);
    std::printf("\n");

    std::printf("on grid  : %d vegetated cells, tallest %.1f m, mean %.1f m\n",
                vegetated, tallest * 0.1,
                vegetated ? (double)sum / vegetated * 0.1 : 0.0);

    // A 5 km path at 10 m above ground, straight across the middle of the area.
    const auto world = live.worldSize();
    const glm::vec3 from{world.x * 0.05f, live.heightAtWorld(world.x * 0.05f, world.z * 0.5f) + 10.0f, world.z * 0.5f};
    const glm::vec3 to  {world.x * 0.95f, live.heightAtWorld(world.x * 0.95f, world.z * 0.5f) + 10.0f, world.z * 0.5f};
    const float pathKm = glm::length(glm::vec2(to.x - from.x, to.z - from.z)) / 1000.0f;
    const float depth = live.foliageDepthM(from, to);
    std::printf("path     : %.2f km, foliage depth %.0f m (%.0f%% of path)\n",
                pathKm, depth, 100.0 * depth / (pathKm * 1000.0));
    for (float f : {150.0f, 450.0f, 900.0f, 1800.0f, 5800.0f}) {
        std::printf("   %6.0f MHz -> %5.1f dB vegetation loss\n",
                    f, RFModels::vegetationLossDb(depth, f));
    }
    // Render the map wash so the colour ramp, the alpha and - the part a test
    // cannot see - the north/south orientation can be looked at.
    const QImage wash = CanopyShade::render(live);
    if (wash.isNull()) {
        std::printf("wash     : nothing to draw\n");
    } else {
        const QString out = argc > 3 ? argv[3] : "canopy-wash.png";
        const bool saved = wash.scaled(wash.width() * 2, wash.height() * 2,
                                       Qt::KeepAspectRatio, Qt::FastTransformation)
                               .save(out, "PNG");
        std::printf("wash     : %dx%d -> %s (%s)\n", wash.width(), wash.height(),
                    out.toUtf8().constData(), saved ? "saved" : "FAILED");
    }
    return 0;
}
