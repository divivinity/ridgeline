#pragma once
#include "TerrainModel.h"

#include <cstdint>
#include <string>
#include <vector>

// Hand-drawn stands of vegetation.
//
// Stored as areas rather than as the raster they produce. A 1024x1024 canopy
// grid is megabytes and cannot be edited once written; a handful of rectangles
// is a few hundred bytes, survives a terrain change, and can be adjusted or
// removed afterwards. The grid is rebuilt from these whenever terrain or areas
// change.
//
// Coordinates are terrain world metres, matching source positions, so an area
// keeps its meaning when the grid resolution changes.
struct VegetationArea {
    float minX = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxZ = 0.0f;
    // Canopy height above ground, in metres above the bare earth beneath it.
    float canopyHeightM = 15.0f;
    std::string name;

    bool contains(float worldX, float worldZ) const {
        return worldX >= minX && worldX <= maxX && worldZ >= minZ && worldZ <= maxZ;
    }
    bool valid() const { return maxX > minX && maxZ > minZ && canopyHeightM > 0.0f; }
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

// Rebuilds terrain.canopyHeightDm and terrain.canopyCoverPct from an imported
// base layer and the hand-drawn areas on top of it.
//
// `baseHeightDm` and `baseCoverPct`, when non-empty, must already be resampled
// onto this terrain's grid. Either may be empty: no import, or an import that
// carries height but no cover measurement.
//
// A drawn area *replaces* what is underneath rather than taking the taller of
// the two, because the operator drew it knowing what was there. Where drawn
// areas overlap each other the tallest still wins: two stands on the same
// ground are one canopy, not two stacked ones.
//
// A drawn stand carries no cover measurement, so it is treated as complete
// cover. Drawing over imported 45%-cover forest at the same height therefore
// increases the predicted loss - that is the intended reading of "I know there
// are trees here", not an accident.
//
// Both grids are cleared when nothing applies, so a scene with no vegetation
// costs nothing and reports hasVegetation() == false.
void apply(TerrainModel& terrain,
           const std::vector<VegetationArea>& areas,
           const std::vector<std::uint16_t>& baseHeightDm = {},
           const std::vector<std::uint8_t>& baseCoverPct = {});

} // namespace VegetationAreas
