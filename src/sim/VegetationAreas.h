#pragma once
#include "TerrainModel.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// What a drawn area says about the ground beneath it.
//
// A stand is trees of a stated height. A clearing is felled ground: it removes
// canopy, imported or drawn, rather than adding any. Clearings exist because
// imported canopy describes a past year and drawn stands are the only canopy
// outside the United States; in both cases an operator needs to say "the trees
// here are gone" without redrawing everything around the gap.
enum class VegetationKind : std::uint8_t { Stand, Clearing };

// Hand-drawn stands and clearings.
//
// Stored as areas rather than as the raster they produce. A 1024x1024 canopy
// grid is megabytes and cannot be edited once written; a handful of rectangles
// is a few hundred bytes, survives a terrain change, and can be adjusted or
// removed afterwards. The grid is rebuilt from these whenever terrain or areas
// change.
//
// Coordinates are terrain world metres from the south-west sample, matching
// source positions, so an area keeps its meaning across a change of grid
// resolution over the same footprint. Across a change of footprint the frame's
// origin moves, so SimEngine::setTerrain carries areas by latitude and
// longitude when both terrains are georeferenced. Where either is not there is
// no shared frame, and areas carry over verbatim in metres.
struct VegetationArea {
    float minX = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxZ = 0.0f;
    // Canopy height above ground, in metres above the bare earth beneath it.
    // Ignored for clearings.
    float canopyHeightM = 15.0f;
    std::string name;
    // Appended last so every existing aggregate initialiser
    // {minX, minZ, maxX, maxZ, height, name} still compiles and still means a
    // stand.
    VegetationKind kind = VegetationKind::Stand;

    bool contains(float worldX, float worldZ) const {
        return worldX >= minX && worldX <= maxX && worldZ >= minZ && worldZ <= maxZ;
    }

    // Inline on purpose: ScenarioIO calls it, and the scenario-save test target
    // links no VegetationAreas.cpp.
    //
    // Bounds must be finite and within fifty thousand kilometres. A hand-edited
    // 1e39 reads back from JSON as infinity, and before this check it reached a
    // float-to-int conversion in the rasteriser, which is undefined behaviour.
    // A clearing needs no height; a stand needs a positive one, and NaN fails
    // that comparison.
    bool valid() const {
        // Generous enough for an area carried by latitude and longitude to the
        // far side of the world - about 2e7 m on either axis, plus the terrain's
        // own extent - so a long location jump never makes a drawn area invalid.
        constexpr float kLimitM = 5.0e7f;
        const auto ok = [](float v) { return std::isfinite(v) && std::abs(v) <= kLimitM; };
        return ok(minX) && ok(minZ) && ok(maxX) && ok(maxZ) &&
               maxX > minX && maxZ > minZ &&
               (kind == VegetationKind::Clearing || canopyHeightM > 0.0f);
    }
};

// What an imported canopy layer is, in terms the simulation core can display
// without knowing anything about where it came from or how it is fetched.
struct CanopySourceInfo {
    std::string displayName;    // "LANDFIRE canopy height and cover"
    std::string vintage;        // "LANDFIRE LF2024"
    std::string licence;        // "Public domain (US Government work)"
    std::string attribution;    // credit line to carry into exports
    std::string retrievedUtc;
    double sourceResolutionM = 0.0;

    int vegetatedSamples = 0;
    // Samples whose height the provider assigned rather than measured. Surfaced
    // so an operator is never shown a substituted number as though it were
    // data.
    int assignedSamples = 0;

    bool empty() const { return displayName.empty(); }
};

namespace VegetationAreas {

// The tallest stand that can be drawn by hand. Well above any tree, and far
// below what the sixteen-bit decimetre grid can hold; the cap exists to catch a
// typo, not to express a physical limit.
constexpr float kMaxCanopyHeightM = 100.0f;

// Inline for the same reason as VegetationArea::valid(): ScenarioIO uses them.
inline const char* kindToString(VegetationKind kind) {
    return kind == VegetationKind::Clearing ? "clearing" : "stand";
}

inline bool kindFromString(const std::string& text, VegetationKind& out) {
    if (text == "stand")    { out = VegetationKind::Stand;    return true; }
    if (text == "clearing") { out = VegetationKind::Clearing; return true; }
    return false;
}

// The samples an area paints, and the ground the solver attributes to them.
//
// The painted range rounds outward - floor(min / cell) to ceil(max / cell),
// both inclusive - so it can reach up to one sample beyond the drawn rectangle
// on each side, and a narrow clearing over-clears by up to a cell each way. It
// is kept rather than tightened to sample-centre inclusion because saved
// scenarios, including calibration corrections fitted against their coverage,
// were produced with it, and changing it would silently change their answers.
// The editing view draws this footprint beside the outline, so the difference
// is shown rather than hidden.
//
// minX..maxZ is the ground the nearest-sample canopy reads attribute to the
// painted samples: half a sample beyond the outermost ones, clipped to the
// terrain. Empty for an invalid area or one entirely off the terrain.
struct Footprint {
    bool empty = true;
    int x0 = 0, x1 = -1, z0 = 0, z1 = -1;
    std::size_t samples = 0;
    float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
};
Footprint footprint(const TerrainModel& terrain, const VegetationArea& area);

// True when two area lists produce the same canopy grid on any terrain: the
// same count and, index by index, the same kind, validity, bounds and - for
// stands - the same height in the grid's decimetres. Names are ignored, so a
// rename neither rebuilds the canopy nor throws away a solve whose inputs did
// not change.
bool sameFootprint(const std::vector<VegetationArea>& a,
                   const std::vector<VegetationArea>& b);

// Valid areas that paint no sample of this terrain, typically carried off it by
// a change of location. They stay in the scenario but have no effect.
int countOutside(const TerrainModel& terrain, const std::vector<VegetationArea>& areas);

// Imported canopy under an area's footprint, so a new stand can take its height
// from measured data instead of a flat default.
struct ImportedUnder {
    std::size_t samples = 0;
    std::size_t vegetated = 0;
    float medianM = 0.0f;   // over vegetated samples only
};
ImportedUnder importedHeightUnder(const TerrainModel& terrain,
                                  const VegetationArea& area,
                                  const std::vector<std::uint16_t>& baseHeightDm);

// Rebuilds terrain.canopyHeightDm and terrain.canopyCoverPct from an imported
// base layer and the hand-drawn areas on top of it.
//
// `baseHeightDm` and `baseCoverPct`, when non-empty, must already be resampled
// onto this terrain's grid. Either may be empty: no import, or an import that
// carries height but no cover measurement.
//
// A drawn area *replaces* what is imported beneath it, because the operator
// drew it knowing what was there. Among drawn areas two rules hold, and neither
// depends on list order, so file order, undo order and drawing order never
// change a result:
//   - A clearing beats every stand it overlaps. Outside the United States drawn
//     stands are the only canopy, so a clearing has to be able to cut a landing
//     zone or a road out of a drawn wood without the wood being split. The cost
//     is that a copse left standing inside a clearing needs the clearing split
//     around it.
//   - Where stands overlap each other the tallest wins: two stands on the same
//     ground are one canopy, not two stacked ones.
//
// A drawn stand carries no cover measurement, so it is treated as complete
// cover. Drawing over imported 45%-cover forest at the same height therefore
// increases the predicted loss - that is the intended reading of "I know there
// are trees here", not an accident.
//
// A clearing sets height to zero and leaves cover as it was. With no canopy the
// cover value changes no answer, and leaving it means a clearing over ground
// that was already bare produces a byte-identical grid, so it does not
// invalidate a solve it cannot have changed.
//
// Both grids are cleared when nothing applies, so a scene with no vegetation
// costs nothing and reports hasVegetation() == false.
void apply(TerrainModel& terrain,
           const std::vector<VegetationArea>& areas,
           const std::vector<std::uint16_t>& baseHeightDm = {},
           const std::vector<std::uint8_t>& baseCoverPct = {});

} // namespace VegetationAreas
