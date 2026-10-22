#include "VegetationAreas.h"

#include <algorithm>
#include <cmath>

namespace {

// A stand's height in the grid's decimetres. Clamped rather than wrapped, so an
// implausible height paints the tallest representable canopy instead of a
// short one.
std::uint16_t standValueDm(const VegetationArea& area) {
    const float clamped =
        std::clamp(area.canopyHeightM, 0.0f, VegetationAreas::kMaxCanopyHeightM);
    return static_cast<std::uint16_t>(std::lround(clamped * 10.0f));
}

} // namespace

VegetationAreas::Footprint VegetationAreas::footprint(const TerrainModel& terrain,
                                                      const VegetationArea& area) {
    Footprint fp;
    if (!area.valid() || terrain.width < 1 || terrain.height < 1) return fp;
    const double cellX = terrain.metersPerCellX();
    const double cellZ = terrain.metersPerCellZ();
    if (!(cellX > 0.0) || !(cellZ > 0.0)) return fp;

    // The quotient is taken in float, as the rasteriser has always taken it. A
    // bound typed as a decimal multiple of the cell size divides to a whole
    // number in float but not always in double, so dividing in double would move
    // that edge by a sample and change what an existing scenario paints. Only
    // then is it widened and clamped in double before any integer conversion:
    // valid() bounds the inputs, but on a fine grid a large rectangle divided by
    // the cell size can still exceed the range of int.
    const float cellXf = terrain.metersPerCellX();
    const float cellZf = terrain.metersPerCellZ();
    const double lastX = static_cast<double>(terrain.width - 1);
    const double lastZ = static_cast<double>(terrain.height - 1);
    const double x0 = std::floor(static_cast<double>(area.minX / cellXf));
    const double x1 = std::ceil(static_cast<double>(area.maxX / cellXf));
    const double z0 = std::floor(static_cast<double>(area.minZ / cellZf));
    const double z1 = std::ceil(static_cast<double>(area.maxZ / cellZf));
    if (x1 < 0.0 || x0 > lastX || z1 < 0.0 || z0 > lastZ) return fp;

    fp.x0 = static_cast<int>(std::clamp(x0, 0.0, lastX));
    fp.x1 = static_cast<int>(std::clamp(x1, 0.0, lastX));
    fp.z0 = static_cast<int>(std::clamp(z0, 0.0, lastZ));
    fp.z1 = static_cast<int>(std::clamp(z1, 0.0, lastZ));
    fp.empty = false;
    fp.samples = static_cast<std::size_t>(fp.x1 - fp.x0 + 1) *
                 static_cast<std::size_t>(fp.z1 - fp.z0 + 1);

    const glm::vec3 world = terrain.worldSize();
    fp.minX = static_cast<float>(std::max(0.0, (fp.x0 - 0.5) * cellX));
    fp.maxX = static_cast<float>(std::min(static_cast<double>(world.x), (fp.x1 + 0.5) * cellX));
    fp.minZ = static_cast<float>(std::max(0.0, (fp.z0 - 0.5) * cellZ));
    fp.maxZ = static_cast<float>(std::min(static_cast<double>(world.z), (fp.z1 + 0.5) * cellZ));
    return fp;
}

bool VegetationAreas::sameFootprint(const std::vector<VegetationArea>& a,
                                    const std::vector<VegetationArea>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const VegetationArea& p = a[i];
        const VegetationArea& q = b[i];
        if (p.kind != q.kind || p.valid() != q.valid()) return false;
        // Exact comparison on purpose: the grid is a function of these values,
        // and a tolerance would let a real move be mistaken for no change.
        if (p.minX != q.minX || p.minZ != q.minZ ||
            p.maxX != q.maxX || p.maxZ != q.maxZ) return false;
        if (p.kind == VegetationKind::Stand && standValueDm(p) != standValueDm(q))
            return false;
    }
    return true;
}

int VegetationAreas::countOutside(const TerrainModel& terrain,
                                  const std::vector<VegetationArea>& areas) {
    int outside = 0;
    for (const VegetationArea& area : areas)
        if (area.valid() && footprint(terrain, area).empty) ++outside;
    return outside;
}

VegetationAreas::ImportedUnder VegetationAreas::importedHeightUnder(
        const TerrainModel& terrain,
        const VegetationArea& area,
        const std::vector<std::uint16_t>& baseHeightDm) {
    ImportedUnder under;
    const Footprint fp = footprint(terrain, area);
    if (fp.empty || baseHeightDm.size() != terrain.heights.size()) return under;

    std::vector<std::uint16_t> vegetated;
    vegetated.reserve(fp.samples);
    for (int z = fp.z0; z <= fp.z1; ++z) {
        for (int x = fp.x0; x <= fp.x1; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) * terrain.width + x;
            if (index >= baseHeightDm.size()) continue;
            ++under.samples;
            if (baseHeightDm[index] > 0) vegetated.push_back(baseHeightDm[index]);
        }
    }
    under.vegetated = vegetated.size();
    if (!vegetated.empty()) {
        // The upper median, which is an actual sample value. Imported canopy
        // is quantised into classes, and averaging two classes would suggest a
        // height the data never reported.
        const auto middle = vegetated.begin() + static_cast<std::ptrdiff_t>(vegetated.size() / 2);
        std::nth_element(vegetated.begin(), middle, vegetated.end());
        under.medianM = static_cast<float>(*middle) * 0.1f;
    }
    return under;
}

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

    if (anyArea) {
        // Drawn areas are rasterised on their own first, so their rules apply
        // among themselves while the result still replaces the imported base
        // wholesale. A claim records which rule decided a sample, because the
        // height buffer alone cannot tell a cleared sample from an untouched
        // one: zero means both.
        constexpr std::uint8_t kUntouched = 0;
        constexpr std::uint8_t kStand = 1;
        constexpr std::uint8_t kClearing = 2;
        std::vector<std::uint16_t> drawn(samples, 0);
        std::vector<std::uint8_t> claim(samples, kUntouched);

        for (const VegetationArea& area : areas) {
            const Footprint fp = footprint(terrain, area);
            if (fp.empty) continue;
            const bool clearing = area.kind == VegetationKind::Clearing;
            const std::uint16_t value = clearing ? 0 : standValueDm(area);
            // A stand too short for the grid's decimetres paints nothing, as
            // before clearings existed.
            if (!clearing && value == 0) continue;

            for (int z = fp.z0; z <= fp.z1; ++z) {
                for (int x = fp.x0; x <= fp.x1; ++x) {
                    const std::size_t index =
                        static_cast<std::size_t>(z) * terrain.width + x;
                    if (index >= samples) continue;
                    if (clearing) {
                        // Overwrites a stand painted earlier in the list...
                        claim[index] = kClearing;
                        drawn[index] = 0;
                    } else if (claim[index] != kClearing) {
                        // ...and a stand later in the list cannot repaint it,
                        // which is what makes the result order-independent.
                        claim[index] = kStand;
                        drawn[index] = std::max(drawn[index], value);
                    }
                }
            }
        }

        for (std::size_t i = 0; i < samples; ++i) {
            if (claim[i] == kStand) {
                heights[i] = drawn[i];
                covers[i] = 100;
            } else if (claim[i] == kClearing) {
                heights[i] = 0;
            }
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
