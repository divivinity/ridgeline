#include "VegetationAreas.h"

#include <algorithm>
#include <cmath>

void VegetationAreas::apply(TerrainModel& terrain,
                            const std::vector<VegetationArea>& areas,
                            const std::vector<std::uint16_t>& baseHeightDm,
                            const std::vector<std::uint8_t>& baseCoverPct) {
    const std::size_t samples = terrain.heights.size();
    auto clearAll = [&terrain] {
        terrain.canopyHeightDm.clear();
        terrain.canopyCoverPct.clear();
    };

    if (samples == 0 || terrain.width < 1 || terrain.height < 1) {
        clearAll();
        return;
    }

    const float cellX = terrain.metersPerCellX();
    const float cellZ = terrain.metersPerCellZ();
    if (!(cellX > 0.0f) || !(cellZ > 0.0f)) {
        clearAll();
        return;
    }

    const bool anyArea = std::any_of(areas.begin(), areas.end(),
                                     [](const VegetationArea& a) { return a.valid(); });
    // A base sized for a different grid describes different ground; refusing it
    // is the only safe reading, because using it would shift every stand.
    const bool haveBase = baseHeightDm.size() == samples;
    const bool haveBaseCover = haveBase && baseCoverPct.size() == samples;

    // A scene with nothing to draw must cost nothing and report no vegetation,
    // rather than carrying megabytes of zeroes that hasVegetation() would then
    // treat as present.
    if (!anyArea && !haveBase) {
        clearAll();
        return;
    }

    std::vector<std::uint16_t> heights =
        haveBase ? baseHeightDm : std::vector<std::uint16_t>(samples, 0);
    std::vector<std::uint8_t> covers =
        haveBaseCover ? baseCoverPct : std::vector<std::uint8_t>(samples, 100);

    // Drawn stands are rasterised on their own first, so that "tallest wins"
    // applies among them while the result still replaces the imported base
    // wholesale. Merging in place would let an imported value beat a shorter
    // drawn stand, which is the opposite of what drawing one means.
    if (anyArea) {
        std::vector<std::uint16_t> drawn(samples, 0);
        for (const VegetationArea& area : areas) {
            if (!area.valid()) continue;
            const float clamped =
                std::clamp(area.canopyHeightM, 0.0f, kMaxCanopyHeightM);
            const auto value = static_cast<std::uint16_t>(std::lround(clamped * 10.0f));
            if (value == 0) continue;

            // Half-open in samples, clamped to the grid: an area partly off the
            // terrain still paints the part that is on it.
            const int x0 = std::max(0, static_cast<int>(std::floor(area.minX / cellX)));
            const int x1 = std::min(terrain.width - 1,
                                    static_cast<int>(std::ceil(area.maxX / cellX)));
            const int z0 = std::max(0, static_cast<int>(std::floor(area.minZ / cellZ)));
            const int z1 = std::min(terrain.height - 1,
                                    static_cast<int>(std::ceil(area.maxZ / cellZ)));

            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    const std::size_t index =
                        static_cast<std::size_t>(z) * terrain.width + x;
                    if (index >= drawn.size()) continue;
                    drawn[index] = std::max(drawn[index], value);
                }
            }
        }
        for (std::size_t i = 0; i < samples; ++i) {
            if (drawn[i] == 0) continue;
            heights[i] = drawn[i];
            covers[i]  = 100;
        }
    }

    if (std::none_of(heights.begin(), heights.end(),
                     [](std::uint16_t v) { return v > 0; })) {
        clearAll();
        return;
    }

    terrain.canopyHeightDm = std::move(heights);
    // Cover is only worth carrying when it says something a blanket 100% does
    // not. An all-100 grid is a megabyte that changes no answer.
    if (std::all_of(covers.begin(), covers.end(),
                    [](std::uint8_t v) { return v >= 100; }))
        terrain.canopyCoverPct.clear();
    else
        terrain.canopyCoverPct = std::move(covers);
}
