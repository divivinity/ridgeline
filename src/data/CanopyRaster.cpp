#include "CanopyRaster.h"

#include <algorithm>
#include <cmath>

namespace {

// Half a sample of slack at the edges, so a terrain grid whose outermost row
// lands exactly on the canopy raster's boundary is not treated as outside it.
bool withinSpan(double value, double low, double high, double slack) {
    return value >= low - slack && value <= high + slack;
}

} // namespace

bool CanopyRaster::valid() const {
    if (width <= 0 || height <= 0) return false;
    if (heightDm.size() != static_cast<std::size_t>(width) * height) return false;
    if (!coverPct.empty() &&
        coverPct.size() != static_cast<std::size_t>(width) * height) return false;
    if (!(latStepDeg > 0.0) || !(lonStepDeg > 0.0)) return false;
    if (!std::isfinite(northLatDeg) || !std::isfinite(westLonDeg)) return false;
    if (northLatDeg > 90.0 || southLatDeg() < -90.0) return false;
    return coverage.vegetated > 0;
}

bool CanopyRaster::contains(double latDeg, double lonDeg) const {
    if (width <= 0 || height <= 0) return false;
    return withinSpan(latDeg, southLatDeg(), northLatDeg, latStepDeg * 0.5) &&
           withinSpan(lonDeg, westLonDeg, eastLonDeg(), lonStepDeg * 0.5);
}

std::uint16_t CanopyRaster::heightDmAt(double latDeg, double lonDeg) const {
    if (heightDm.empty() || !contains(latDeg, lonDeg)) return 0;
    const int col = std::clamp(
        static_cast<int>(std::floor((lonDeg - westLonDeg) / lonStepDeg)), 0, width - 1);
    const int row = std::clamp(
        static_cast<int>(std::floor((northLatDeg - latDeg) / latStepDeg)), 0, height - 1);
    return heightDm[static_cast<std::size_t>(row) * width + col];
}

std::uint8_t CanopyRaster::coverPctAt(double latDeg, double lonDeg) const {
    if (coverPct.empty()) return 100;
    if (!contains(latDeg, lonDeg)) return 100;
    const int col = std::clamp(
        static_cast<int>(std::floor((lonDeg - westLonDeg) / lonStepDeg)), 0, width - 1);
    const int row = std::clamp(
        static_cast<int>(std::floor((northLatDeg - latDeg) / latStepDeg)), 0, height - 1);
    return coverPct[static_cast<std::size_t>(row) * width + col];
}

bool CanopyRaster::sampleOnto(const TerrainModel& terrain,
                              std::vector<std::uint16_t>& outHeightDm,
                              std::vector<std::uint8_t>& outCoverPct) const {
    if (heightDm.empty()) return false;
    if (!terrain.hasGeoReference()) return false;
    if (terrain.width <= 0 || terrain.height <= 0) return false;

    const std::size_t cells =
        static_cast<std::size_t>(terrain.width) * terrain.height;
    std::vector<std::uint16_t> heights(cells, 0);
    std::vector<std::uint8_t>  covers(cells, 0);

    const float stepX = terrain.metersPerCellX();
    const float stepZ = terrain.metersPerCellZ();
    const bool haveCover = !coverPct.empty();

    std::size_t overlapping = 0;
    for (int z = 0; z < terrain.height; ++z) {
        for (int x = 0; x < terrain.width; ++x) {
            const TerrainModel::GeoPoint p = terrain.worldToLatLon(
                static_cast<float>(x) * stepX, static_cast<float>(z) * stepZ);
            if (!contains(p.latDeg, p.lonDeg)) continue;
            ++overlapping;
            const std::size_t index = static_cast<std::size_t>(z) * terrain.width + x;
            heights[index] = heightDmAt(p.latDeg, p.lonDeg);
            covers[index]  = haveCover ? coverPctAt(p.latDeg, p.lonDeg)
                                       : static_cast<std::uint8_t>(100);
        }
    }

    // No overlap at all means this canopy describes somewhere else. Installing
    // it would put an empty vegetation grid on the terrain and report the scene
    // as known-bare, which is a stronger claim than "we have no data here".
    if (overlapping == 0) return false;

    outHeightDm = std::move(heights);
    outCoverPct = haveCover ? std::move(covers) : std::vector<std::uint8_t>();
    return true;
}
