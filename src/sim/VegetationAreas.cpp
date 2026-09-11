#include "VegetationAreas.h"

#include <algorithm>
#include <cmath>

void VegetationAreas::apply(TerrainModel& terrain,
                            const std::vector<VegetationArea>& areas) {
    const std::size_t samples = terrain.heights.size();
    if (samples == 0 || terrain.width < 1 || terrain.height < 1) {
        terrain.canopyHeightDm.clear();
        return;
    }

    // A scene with nothing to draw must cost nothing and report no vegetation,
    // rather than carrying a megabyte of zeroes that hasVegetation() would
    // then treat as present.
    const bool anyValid = std::any_of(areas.begin(), areas.end(),
                                      [](const VegetationArea& a) { return a.valid(); });
    if (!anyValid) {
        terrain.canopyHeightDm.clear();
        return;
    }

    const float cellX = terrain.metersPerCellX();
    const float cellZ = terrain.metersPerCellZ();
    if (!(cellX > 0.0f) || !(cellZ > 0.0f)) {
        terrain.canopyHeightDm.clear();
        return;
    }

    terrain.canopyHeightDm.assign(samples, 0);
    for (const VegetationArea& area : areas) {
        if (!area.valid()) continue;
        const float clamped = std::clamp(area.canopyHeightM, 0.0f, kMaxCanopyHeightM);
        const auto value = static_cast<std::uint8_t>(std::lround(clamped * 10.0f));
        if (value == 0) continue;

        // Half-open in samples, clamped to the grid: an area partly off the
        // terrain still paints the part that is on it.
        const int x0 = std::max(0, static_cast<int>(std::floor(area.minX / cellX)));
        const int x1 = std::min(terrain.width - 1, static_cast<int>(std::ceil(area.maxX / cellX)));
        const int z0 = std::max(0, static_cast<int>(std::floor(area.minZ / cellZ)));
        const int z1 = std::min(terrain.height - 1, static_cast<int>(std::ceil(area.maxZ / cellZ)));

        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const std::size_t index = static_cast<std::size_t>(z) * terrain.width + x;
                if (index >= terrain.canopyHeightDm.size()) continue;
                // Tallest wins: two stands on the same ground are one canopy,
                // not two stacked ones.
                terrain.canopyHeightDm[index] =
                    std::max(terrain.canopyHeightDm[index], value);
            }
        }
    }

    // Every area fell outside the terrain, so there is still nothing to draw.
    if (std::none_of(terrain.canopyHeightDm.begin(), terrain.canopyHeightDm.end(),
                     [](std::uint8_t v) { return v > 0; }))
        terrain.canopyHeightDm.clear();
}
