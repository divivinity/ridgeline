#include "map/VegetationGeometry.h"
#include "render/TerrainTriangulation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace VegetationGeometry {
namespace {

constexpr Grip kCornerOrder[4] = {
    Grip::MinXMinZ, Grip::MaxXMinZ, Grip::MaxXMaxZ, Grip::MinXMaxZ
};

} // namespace

Rect boundsOf(const VegetationArea& area) {
    return {area.minX, area.minZ, area.maxX, area.maxZ};
}

Rect fromCorners(glm::vec2 a, glm::vec2 b, glm::vec2 extent) {
    Rect rect;
    rect.minX = std::clamp(std::min(a.x, b.x), 0.0f, extent.x);
    rect.maxX = std::clamp(std::max(a.x, b.x), 0.0f, extent.x);
    rect.minZ = std::clamp(std::min(a.y, b.y), 0.0f, extent.y);
    rect.maxZ = std::clamp(std::max(a.y, b.y), 0.0f, extent.y);
    return rect;
}

bool coversACell(const Rect& rect, const TerrainModel& terrain) {
    return (rect.maxX - rect.minX) >= terrain.metersPerCellX() &&
           (rect.maxZ - rect.minZ) >= terrain.metersPerCellZ();
}

int pick(const std::vector<VegetationArea>& areas, float x, float z) {
    int best = -1;
    double bestSize = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < areas.size(); ++i) {
        const VegetationArea& area = areas[i];
        if (!area.valid() || !area.contains(x, z)) continue;
        const double size = static_cast<double>(area.maxX - area.minX) *
                            static_cast<double>(area.maxZ - area.minZ);
        if (size <= bestSize) {
            bestSize = size;
            best = static_cast<int>(i);
        }
    }
    return best;
}

Grip hitTest(const Rect& selected, const ProjectedCorners& corners,
             glm::vec2 cursorPx, bool haveWorldHit, glm::vec2 worldHit,
             float gripPx) {
    float shortestEdge = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t j = (i + 1) % 4;
        if (corners.visible[i] && corners.visible[j])
            shortestEdge = std::min(shortestEdge, glm::length(corners.px[i] - corners.px[j]));
    }
    const float radius = std::isfinite(shortestEdge)
        ? std::min(gripPx, shortestEdge / 3.0f) : gripPx;

    Grip best = Grip::None;
    float bestDistance = radius;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!corners.visible[i]) continue;
        const float distance = glm::length(corners.px[i] - cursorPx);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = kCornerOrder[i];
        }
    }
    if (best != Grip::None) return best;

    if (haveWorldHit &&
        worldHit.x >= selected.minX && worldHit.x <= selected.maxX &&
        worldHit.y >= selected.minZ && worldHit.y <= selected.maxZ)
        return Grip::Body;
    return Grip::None;
}

Rect reshape(const Rect& original, Grip grip, glm::vec2 anchor, glm::vec2 cursor,
             glm::vec2 extent) {
    if (grip == Grip::None) return original;

    if (grip == Grip::Body) {
        const glm::vec2 delta = cursor - anchor;
        Rect moved{original.minX + delta.x, original.minZ + delta.y,
                   original.maxX + delta.x, original.maxZ + delta.y};
        // An area carried partly off the terrain is allowed to stay partly off;
        // pulling it on would change its size or silently move ground it
        // already describes.
        const bool startedInside =
            original.minX >= 0.0f && original.minZ >= 0.0f &&
            original.maxX <= extent.x && original.maxZ <= extent.y;
        if (startedInside) {
            const auto keepInside = [](float& lo, float& hi, float limit) {
                if (lo < 0.0f) { hi -= lo; lo = 0.0f; }
                if (hi > limit) { lo -= hi - limit; hi = limit; }
            };
            keepInside(moved.minX, moved.maxX, extent.x);
            keepInside(moved.minZ, moved.maxZ, extent.y);
        }
        return moved;
    }

    glm::vec2 corner{0.0f};
    glm::vec2 opposite{0.0f};
    switch (grip) {
        case Grip::MinXMinZ: corner = {original.minX, original.minZ};
                             opposite = {original.maxX, original.maxZ}; break;
        case Grip::MaxXMinZ: corner = {original.maxX, original.minZ};
                             opposite = {original.minX, original.maxZ}; break;
        case Grip::MaxXMaxZ: corner = {original.maxX, original.maxZ};
                             opposite = {original.minX, original.minZ}; break;
        case Grip::MinXMaxZ: corner = {original.minX, original.maxZ};
                             opposite = {original.maxX, original.minZ}; break;
        default: return original;
    }
    // The grab offset is kept, so a corner grabbed a few pixels inside does not
    // jump to the cursor on the first move.
    glm::vec2 dragged = cursor + (corner - anchor);
    dragged.x = std::clamp(dragged.x, 0.0f, extent.x);
    dragged.y = std::clamp(dragged.y, 0.0f, extent.y);
    return {std::min(dragged.x, opposite.x), std::min(dragged.y, opposite.y),
            std::max(dragged.x, opposite.x), std::max(dragged.y, opposite.y)};
}

float meshHeightAtWorld(const TerrainModel& terrain, float x, float z) {
    if (terrain.width < 2 || terrain.height < 2 || terrain.heights.empty()) return 0.0f;
    if (!std::isfinite(x) || !std::isfinite(z)) return 0.0f;
    const float cellX = terrain.metersPerCellX();
    const float cellZ = terrain.metersPerCellZ();
    if (!(cellX > 0.0f) || !(cellZ > 0.0f)) return 0.0f;

    const float gx = x / cellX;
    const float gz = z / cellZ;
    // Clamped in float before the integer conversion, so a point far off the
    // terrain reads the nearest edge cell instead of being undefined behaviour.
    const int cx = static_cast<int>(std::clamp(std::floor(gx), 0.0f,
                                               static_cast<float>(terrain.width - 2)));
    const int cz = static_cast<int>(std::clamp(std::floor(gz), 0.0f,
                                               static_cast<float>(terrain.height - 2)));
    const float fx = std::clamp(gx - static_cast<float>(cx), 0.0f, 1.0f);
    const float fz = std::clamp(gz - static_cast<float>(cz), 0.0f, 1.0f);
    return TerrainTriangulation::heightInCell(
        terrain.heightAt(cx, cz), terrain.heightAt(cx + 1, cz),
        terrain.heightAt(cx, cz + 1), terrain.heightAt(cx + 1, cz + 1), fx, fz);
}

glm::vec2 meshSlopeAtWorld(const TerrainModel& terrain, float x, float z) {
    if (terrain.width < 2 || terrain.height < 2 || terrain.heights.empty()) return glm::vec2(0.0f);
    if (!std::isfinite(x) || !std::isfinite(z)) return glm::vec2(0.0f);
    const float cellX = terrain.metersPerCellX();
    const float cellZ = terrain.metersPerCellZ();
    if (!(cellX > 0.0f) || !(cellZ > 0.0f)) return glm::vec2(0.0f);

    // Located exactly as meshHeightAtWorld locates a point.
    const float gx = x / cellX;
    const float gz = z / cellZ;
    const int cx = static_cast<int>(std::clamp(std::floor(gx), 0.0f,
                                               static_cast<float>(terrain.width - 2)));
    const int cz = static_cast<int>(std::clamp(std::floor(gz), 0.0f,
                                               static_cast<float>(terrain.height - 2)));
    const float fx = std::clamp(gx - static_cast<float>(cx), 0.0f, 1.0f);
    const float fz = std::clamp(gz - static_cast<float>(cz), 0.0f, 1.0f);
    float perFx = 0.0f;
    float perFz = 0.0f;
    TerrainTriangulation::slopeInCell(
        terrain.heightAt(cx, cz), terrain.heightAt(cx + 1, cz),
        terrain.heightAt(cx, cz + 1), terrain.heightAt(cx + 1, cz + 1), fx, fz, perFx, perFz);
    return {perFx / cellX, perFz / cellZ};
}

std::array<glm::vec3, 4> cornersOnMesh(const TerrainModel& terrain, const Rect& rect) {
    const auto at = [&terrain](float x, float z) {
        return glm::vec3{x, meshHeightAtWorld(terrain, x, z), z};
    };
    return {at(rect.minX, rect.minZ), at(rect.maxX, rect.minZ),
            at(rect.maxX, rect.maxZ), at(rect.minX, rect.maxZ)};
}

Style styleFor(VegetationKind kind, Role role) {
    const bool dashed = kind == VegetationKind::Clearing;
    switch (role) {
        case Role::Committed: return {0.80f, 0.60f, 1.00f, 1.00f, dashed};
        case Role::Selected:  return {1.00f, 1.00f, 1.00f, 1.00f, dashed};
        // The solver's footprint is a fact about the grid, not about the kind,
        // so it is never dashed.
        case Role::Footprint: return {1.00f, 1.00f, 1.00f, 0.40f, false};
        case Role::Preview:   return {1.00f, 1.00f, 1.00f, 0.85f, dashed};
    }
    return {0.80f, 0.60f, 1.00f, 1.00f, dashed};
}

void appendOutline(std::vector<LineVertex>& out, const TerrainModel& terrain,
                   const Rect& rect, const Style& style) {
    if (terrain.width < 2 || terrain.height < 2 || terrain.heights.empty()) return;
    const float cellX = terrain.metersPerCellX();
    const float cellZ = terrain.metersPerCellZ();
    if (!(cellX > 0.0f) || !(cellZ > 0.0f)) return;
    const float width = rect.maxX - rect.minX;
    const float depth = rect.maxZ - rect.minZ;
    if (!(width > 0.0f) || !(depth > 0.0f)) return;
    const glm::vec3 world = terrain.worldSize();

    // Walked anticlockwise from the south-west corner. `alongStart` is the
    // distance around the unclipped perimeter where each edge begins, so dashes
    // stay put when part of the rectangle is off the terrain.
    struct Edge {
        bool alongX;       // true: runs east-west at a fixed z
        float fixed;       // the edge's constant coordinate
        float from, to;    // the moving coordinate, in walking direction
        float alongStart;
    };
    const Edge edges[4] = {
        {true,  rect.minZ, rect.minX, rect.maxX, 0.0f},
        {false, rect.maxX, rect.minZ, rect.maxZ, width},
        {true,  rect.maxZ, rect.maxX, rect.minX, width + depth},
        {false, rect.minX, rect.maxZ, rect.minZ, 2.0f * width + depth},
    };

    std::vector<float> stops;
    for (const Edge& edge : edges) {
        const float fixedExtent = edge.alongX ? world.z : world.x;
        if (edge.fixed < 0.0f || edge.fixed > fixedExtent) continue;
        const float movingExtent = edge.alongX ? world.x : world.z;
        const float cell = edge.alongX ? cellX : cellZ;
        const float fixedCell = edge.alongX ? cellZ : cellX;
        const int lastFixedCell = (edge.alongX ? terrain.height : terrain.width) - 2;

        const float lo = std::max(std::min(edge.from, edge.to), 0.0f);
        const float hi = std::min(std::max(edge.from, edge.to), movingExtent);
        if (!(hi > lo)) continue;

        stops.clear();
        stops.push_back(lo);
        stops.push_back(hi);
        const int firstLine = static_cast<int>(std::ceil(lo / cell));
        const int lastLine = static_cast<int>(std::floor(hi / cell));
        for (int k = firstLine; k <= lastLine; ++k) {
            const float value = static_cast<float>(k) * cell;
            if (value > lo && value < hi) stops.push_back(value);
        }

        // Where the cell diagonal crosses this edge. The fixed coordinate's
        // fraction is taken in the same clamped cell meshHeightAtWorld reads,
        // so the break falls exactly where the mesh changes triangle.
        const float fixedGrid = edge.fixed / fixedCell;
        const int fixedIndex = static_cast<int>(std::clamp(
            std::floor(fixedGrid), 0.0f, static_cast<float>(std::max(lastFixedCell, 0))));
        const float fixedFraction =
            std::clamp(fixedGrid - static_cast<float>(fixedIndex), 0.0f, 1.0f);
        for (int k = firstLine - 1; k <= lastLine; ++k) {
            const float value = (static_cast<float>(k) + 1.0f - fixedFraction) * cell;
            if (value > lo && value < hi) stops.push_back(value);
        }

        std::sort(stops.begin(), stops.end());
        const float tolerance = cell * 1.0e-4f;
        stops.erase(std::unique(stops.begin(), stops.end(),
                                [tolerance](float a, float b) { return b - a <= tolerance; }),
                    stops.end());
        if (edge.from > edge.to) std::reverse(stops.begin(), stops.end());

        const auto vertexAt = [&](float moving) {
            const float x = edge.alongX ? moving : edge.fixed;
            const float z = edge.alongX ? edge.fixed : moving;
            return LineVertex{x, meshHeightAtWorld(terrain, x, z), z,
                              style.r, style.g, style.b, style.a,
                              edge.alongStart + std::abs(moving - edge.from),
                              style.dashed ? 1.0f : 0.0f, 0.0f, 0.0f};
        };
        for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
            LineVertex a = vertexAt(stops[i]);
            LineVertex b = vertexAt(stops[i + 1]);
            // Taken at the midpoint, which lies strictly inside the one triangle
            // the whole segment sits on; an end can sit on a shared edge.
            const glm::vec2 slope =
                meshSlopeAtWorld(terrain, (a.x + b.x) * 0.5f, (a.z + b.z) * 0.5f);
            a.slopeX = b.slopeX = slope.x;
            a.slopeZ = b.slopeZ = slope.y;
            out.push_back(a);
            out.push_back(b);
        }
    }
}

} // namespace VegetationGeometry
