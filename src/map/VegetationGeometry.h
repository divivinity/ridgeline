#pragma once
#include "sim/TerrainModel.h"
#include "sim/VegetationAreas.h"

#include <glm/glm.hpp>

#include <array>
#include <vector>

// Geometry for drawing and editing vegetation areas on the map.
//
// Qt-free and GL-free, so every rule that decides what the operator sees and
// what a drag does is tested headlessly: picking, grips, reshaping, and the
// outline draped exactly on the rendered mesh. The viewport only feeds it
// cursor positions and uploads what it returns.
namespace VegetationGeometry {

// Terrain world metres. Axis-aligned, like VegetationArea.
struct Rect {
    float minX = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxZ = 0.0f;
};

Rect boundsOf(const VegetationArea& area);

// The normalised rectangle spanning two corners, clamped to [0, extent]. A drag
// started at any corner produces the same rectangle.
Rect fromCorners(glm::vec2 a, glm::vec2 b, glm::vec2 extent);

// At least one terrain cell on both axes. Anything smaller cannot be told apart
// from its neighbours on the grid, so drawing it would record an area whose
// effect the operator cannot see.
bool coversACell(const Rect& rect, const TerrainModel& terrain);

// The smallest valid area whose closed rectangle contains the point, so a copse
// inside a wood can be selected with a click. On equal size the later index
// wins, matching what is drawn on top. -1 on empty ground.
int pick(const std::vector<VegetationArea>& areas, float x, float z);

enum class Grip { None, Body, MinXMinZ, MaxXMinZ, MaxXMaxZ, MinXMaxZ };

// A selected rectangle's corners as drawn, in widget pixels, in the order
// MinXMinZ, MaxXMinZ, MaxXMaxZ, MinXMaxZ. A corner behind the camera or off the
// screen is marked invisible and cannot be grabbed.
struct ProjectedCorners {
    std::array<glm::vec2, 4> px{};
    std::array<bool, 4> visible{};
};

// Corner grips are found in screen pixels, like antenna markers, so they are as
// easy to grab at any zoom. The grab radius shrinks to a third of the shortest
// visible edge, so on a small or distant area the body stays reachable between
// the corners. A corner within reach beats the body; the body is found from the
// terrain point under the cursor, so it respects what is actually in front.
Grip hitTest(const Rect& selected, const ProjectedCorners& corners,
             glm::vec2 cursorPx, bool haveWorldHit, glm::vec2 worldHit,
             float gripPx = 10.0f);

// Body: translated by cursor - anchor, and kept inside the extent at its own
// size if it started inside. Corner: the grabbed corner follows the cursor with
// the grab offset preserved, the opposite corner stays fixed, and dragging past
// it re-normalises rather than producing an inverted rectangle.
Rect reshape(const Rect& original, Grip grip, glm::vec2 anchor, glm::vec2 cursor,
             glm::vec2 extent);

// Height on the drawn mesh, using the shared cell triangulation. Not
// TerrainModel::heightAtWorld, which interpolates bilinearly and would put a
// line under the visible surface across every twisted cell.
float meshHeightAtWorld(const TerrainModel& terrain, float x, float z);

// A rectangle's corners on the mesh, in the ProjectedCorners order.
std::array<glm::vec3, 4> cornersOnMesh(const TerrainModel& terrain, const Rect& rect);

// Rise per metre east (x) and north (z) of the drawn mesh triangle under a
// point, located and clamped exactly as meshHeightAtWorld locates it. Zero on a
// degenerate terrain or at a non-finite point.
glm::vec2 meshSlopeAtWorld(const TerrainModel& terrain, float x, float z);

// Position in unscaled terrain metres, colour, distance along the outline's
// perimeter (for dashes that do not jump when an edge is clipped), a dashed
// flag, and the slope of the mesh triangle the segment lies on, which the
// shader needs to keep the line in front of the imagery drape when that ground
// is seen edge-on. Laid out to match the outline shader's attributes.
struct LineVertex {
    float x, y, z;
    float r, g, b, a;
    float along;
    float dashed;
    float slopeX, slopeZ;   // rise per metre east and north
};

enum class Role { Committed, Selected, Footprint, Preview };

struct Style {
    float r, g, b, a;
    bool dashed;
};

// Drawn areas use violet, which appears in no other map palette. Clearings are
// dashed and stands solid, so kind never depends on colour alone.
Style styleFor(VegetationKind kind, Role role);

// Appends the rectangle's outline as GL_LINES pairs lying on the mesh. Each edge
// is clipped to the terrain and broken at every grid-line and cell-diagonal
// crossing, so every segment sits inside one triangle and is exact on the
// drawn surface. An edge wholly off the terrain is skipped; a rectangle wholly
// off it adds nothing.
void appendOutline(std::vector<LineVertex>& out, const TerrainModel& terrain,
                   const Rect& rect, const Style& style);

} // namespace VegetationGeometry
