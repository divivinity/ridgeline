#pragma once
#include "TerrainModel.h"

#include <string>
#include <vector>

// Hand-drawn stands of vegetation.
//
// Stored as areas rather than as the raster they produce. A 1024x1024 canopy
// grid is a megabyte and cannot be edited once written; a handful of rectangles
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
    // Canopy height above ground. Clamped to what the decimetre grid can carry.
    float canopyHeightM = 15.0f;
    std::string name;

    bool contains(float worldX, float worldZ) const {
        return worldX >= minX && worldX <= maxX && worldZ >= minZ && worldZ <= maxZ;
    }
    bool valid() const { return maxX > minX && maxZ > minZ && canopyHeightM > 0.0f; }
};

namespace VegetationAreas {

// The tallest canopy a decimetre byte can represent.
constexpr float kMaxCanopyHeightM = 25.5f;

// Rebuilds terrain.canopyHeightDm from the areas. Clears it when no area
// applies, so a scene with no vegetation costs nothing and reports
// hasVegetation() == false.
//
// Overlapping areas take the tallest canopy rather than summing: two stands of
// trees on the same ground are one canopy, not two stacked ones.
void apply(TerrainModel& terrain, const std::vector<VegetationArea>& areas);

} // namespace VegetationAreas
