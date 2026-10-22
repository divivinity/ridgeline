// Headless tests for vegetation editing: the shared cell triangulation, the
// outline draped on the rendered mesh, picking and reshaping, and the undo
// command's merge and interleaving rules. No widgets and no GL context.
#include "data/TerrainLoader.h"
#include "map/VegetationGeometry.h"
#include "render/TerrainTriangulation.h"
#include "sim/TerrainModel.h"
#include "sim/Types.h"
#include "sim/VegetationAreas.h"
#include "ui/SnapshotCommand.h"

#include <QCoreApplication>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}

bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

using VegetationGeometry::Grip;
using VegetationGeometry::LineVertex;
using VegetationGeometry::Rect;
using VegetationGeometry::Role;

// The surface TerrainMesh draws, worked out without VegetationGeometry: the
// cell's vertices placed as the mesh places them, split by the index order the
// mesh draws, and the triangle holding the point interpolated barycentrically.
// Outlines are checked against this rather than against meshHeightAtWorld, so a
// meshHeightAtWorld that drifted off the drawn surface cannot agree with itself.
struct DrawnSurface {
    double height = 0.0;
    double slopeX = 0.0;   // rise per metre east
    double slopeZ = 0.0;   // rise per metre north
};

DrawnSurface drawnMesh(const TerrainModel& terrain, double x, double z) {
    const double cellX = terrain.metersPerCellX();
    const double cellZ = terrain.metersPerCellZ();
    const int cx = std::clamp(static_cast<int>(std::floor(x / cellX)), 0, terrain.width - 2);
    const int cz = std::clamp(static_cast<int>(std::floor(z / cellZ)), 0, terrain.height - 2);
    const auto vertexAt = [&terrain, cellX, cellZ](int sx, int sz) {
        return glm::dvec3(sx * cellX, static_cast<double>(terrain.heightAt(sx, sz)), sz * cellZ);
    };
    // Numbered as TerrainMesh passes them: tl, tr, bl, br.
    const glm::dvec3 corners[4] = {vertexAt(cx, cz), vertexAt(cx + 1, cz),
                                   vertexAt(cx, cz + 1), vertexAt(cx + 1, cz + 1)};
    std::vector<unsigned int> indices;
    TerrainTriangulation::appendCellIndices(indices, 0, 1, 2, 3);

    DrawnSurface surface;
    double bestSlack = -1.0e30;
    for (int t = 0; t < 2; ++t) {
        const glm::dvec3& a = corners[indices[t * 3]];
        const glm::dvec3& b = corners[indices[t * 3 + 1]];
        const glm::dvec3& c = corners[indices[t * 3 + 2]];
        const double det = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
        const double wb = ((x - a.x) * (c.z - a.z) - (c.x - a.x) * (z - a.z)) / det;
        const double wc = ((b.x - a.x) * (z - a.z) - (x - a.x) * (b.z - a.z)) / det;
        const double wa = 1.0 - wb - wc;
        const double slack = std::min({wa, wb, wc});
        if (slack <= bestSlack) continue;
        bestSlack = slack;
        surface.height = wa * a.y + wb * b.y + wc * c.y;
        const glm::dvec3 normal = glm::cross(b - a, c - a);
        surface.slopeX = -normal.x / normal.y;
        surface.slopeZ = -normal.z / normal.y;
    }
    return surface;
}

void testTriangulationMatchesIndices() {
    // tl, tr, bl, br - deliberately non-planar.
    const float h[4] = {1.0f, 4.0f, 2.0f, 9.0f};
    const glm::vec2 corner[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
    std::vector<unsigned int> idx;
    TerrainTriangulation::appendCellIndices(idx, 0, 1, 2, 3);
    check(idx.size() == 6, "a cell is drawn as two triangles");

    const glm::vec2 probes[] = {{0.10f, 0.20f}, {0.45f, 0.50f}, {0.70f, 0.25f}, {0.20f, 0.75f},
                                {0.60f, 0.60f}, {0.90f, 0.80f}, {0.30f, 0.95f}, {0.99f, 0.02f}};
    bool agree = true;
    for (const glm::vec2& p : probes) {
        bool inside = false;
        for (int t = 0; t < 2; ++t) {
            const glm::vec2 a = corner[idx[t * 3]];
            const glm::vec2 b = corner[idx[t * 3 + 1]];
            const glm::vec2 c = corner[idx[t * 3 + 2]];
            const float det = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            const float wb = ((p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y)) / det;
            const float wc = ((b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y)) / det;
            const float wa = 1.0f - wb - wc;
            if (wa < -1e-5f || wb < -1e-5f || wc < -1e-5f) continue;
            inside = true;
            const float plane = wa * h[idx[t * 3]] + wb * h[idx[t * 3 + 1]] + wc * h[idx[t * 3 + 2]];
            const float formula =
                TerrainTriangulation::heightInCell(h[0], h[1], h[2], h[3], p.x, p.y);
            if (!near(plane, formula, 1e-4)) agree = false;
        }
        if (!inside) agree = false;
    }
    check(agree, "heightInCell matches the triangles the index order draws, on both sides of "
                 "the diagonal");
}

void testMeshHeightFollowsDiagonal() {
    TerrainModel twisted = TerrainLoader::makeFlat(2, 2, 10.0f, 0.0f);
    twisted.heights = {0.0f, 0.0f, 0.0f, 10.0f};   // only the north-east sample raised
    const auto mesh = [&twisted](float x, float z) {
        return VegetationGeometry::meshHeightAtWorld(twisted, x, z);
    };
    check(near(mesh(5.0f, 5.0f), 0.0, 1e-5),
          "at a twisted cell's centre the drawn surface is on the flat triangle");
    check(near(mesh(7.5f, 7.5f), 5.0, 1e-5),
          "past the diagonal the drawn surface rises toward the raised corner");
    check(near(mesh(0.0f, 0.0f), 0.0, 1e-5) && near(mesh(10.0f, 0.0f), 0.0, 1e-5) &&
          near(mesh(0.0f, 10.0f), 0.0, 1e-5) && near(mesh(10.0f, 10.0f), 10.0, 1e-5),
          "the drawn surface passes through every sample");
    check(!near(twisted.heightAtWorld(5.0f, 5.0f), mesh(5.0f, 5.0f), 1.0),
          "bilinear interpolation would disagree with what is drawn, which is why it is not used");
    check(near(mesh(-500.0f, 1.0e9f), mesh(0.0f, 10.0f), 1e-5),
          "a point far off the terrain reads the nearest edge instead of misbehaving");
}

void testOutlineLiesOnMesh() {
    const TerrainModel hills = TerrainLoader::makeHills(64, 64, 10.0f, 40.0f);
    std::vector<LineVertex> vertices;
    VegetationGeometry::appendOutline(
        vertices, hills, {37.0f, 53.0f, 412.0f, 298.0f},
        VegetationGeometry::styleFor(VegetationKind::Stand, Role::Committed));

    bool onMesh = !vertices.empty();
    for (const LineVertex& v : vertices)
        if (!near(v.y, drawnMesh(hills, v.x, v.z).height, 2e-3))
            onMesh = false;
    check(onMesh, "every outline vertex lies on the mesh TerrainMesh draws");

    bool midpointsOnMesh = !vertices.empty();
    for (std::size_t i = 0; i + 1 < vertices.size(); i += 2) {
        const float mx = (vertices[i].x + vertices[i + 1].x) * 0.5f;
        const float mz = (vertices[i].z + vertices[i + 1].z) * 0.5f;
        const float my = (vertices[i].y + vertices[i + 1].y) * 0.5f;
        if (!near(my, drawnMesh(hills, mx, mz).height, 2e-3))
            midpointsOnMesh = false;
    }
    check(midpointsOnMesh,
          "every segment sits inside one triangle, so its midpoint is on the mesh too");
    check(vertices.size() > 64,
          "the outline breaks at grid and diagonal crossings, not only at its corners");

    bool heightsAgree = true;
    for (float z = 3.0f; z < 630.0f; z += 7.3f)
        for (float x = 1.0f; x < 630.0f; x += 6.1f)
            if (!near(VegetationGeometry::meshHeightAtWorld(hills, x, z),
                      drawnMesh(hills, x, z).height, 2e-3))
                heightsAgree = false;
    check(heightsAgree, "meshHeightAtWorld agrees with the drawn mesh across the whole terrain");

    bool slopesMatch = !vertices.empty();
    bool anySlope = false;
    for (std::size_t i = 0; i + 1 < vertices.size(); i += 2) {
        const DrawnSurface under = drawnMesh(hills, (vertices[i].x + vertices[i + 1].x) * 0.5,
                                             (vertices[i].z + vertices[i + 1].z) * 0.5);
        for (std::size_t j = i; j <= i + 1; ++j) {
            if (!near(vertices[j].slopeX, under.slopeX, 1e-3) ||
                !near(vertices[j].slopeZ, under.slopeZ, 1e-3))
                slopesMatch = false;
        }
        if (std::abs(under.slopeX) > 0.01 || std::abs(under.slopeZ) > 0.01) anySlope = true;
    }
    check(slopesMatch && anySlope,
          "each segment carries the slope of the drawn triangle under it, for the depth margin");
}

void testOutlineClippingAndStyle() {
    const TerrainModel flat = TerrainLoader::makeFlat(64, 64, 10.0f, 5.0f);
    const glm::vec3 world = flat.worldSize();
    const auto stand = VegetationGeometry::styleFor(VegetationKind::Stand, Role::Committed);

    std::vector<LineVertex> inside;
    VegetationGeometry::appendOutline(inside, flat, {100.0f, 100.0f, 300.0f, 200.0f}, stand);
    check(!inside.empty() && inside.size() % 2 == 0, "an outline is emitted as line pairs");
    check(!inside.empty() && near(inside.front().along, 0.0, 1e-4),
          "distance along the outline starts at zero");
    bool increasing = true;
    float longest = 0.0f;
    for (std::size_t i = 0; i + 1 < inside.size(); i += 2) {
        if (inside[i + 1].along < inside[i].along - 1e-4f) increasing = false;
        longest = std::max(longest, inside[i + 1].along);
    }
    check(increasing, "distance along each edge never runs backwards");
    check(near(longest, 600.0, 1e-3), "the distance around a whole outline is its perimeter");

    std::vector<LineVertex> overhang;
    VegetationGeometry::appendOutline(overhang, flat, {-100.0f, -100.0f, 200.0f, 200.0f}, stand);
    bool clipped = !overhang.empty();
    for (const LineVertex& v : overhang) {
        if (v.x < -1e-4f || v.x > world.x + 1e-4f || v.z < -1e-4f || v.z > world.z + 1e-4f)
            clipped = false;
    }
    check(clipped, "an outline partly off the terrain is clipped to it");

    std::vector<LineVertex> elsewhere;
    VegetationGeometry::appendOutline(elsewhere, flat, {5000.0f, 5000.0f, 6000.0f, 6000.0f}, stand);
    check(elsewhere.empty(), "an outline wholly off the terrain adds nothing");

    std::vector<LineVertex> clearing;
    VegetationGeometry::appendOutline(
        clearing, flat, {100.0f, 100.0f, 300.0f, 200.0f},
        VegetationGeometry::styleFor(VegetationKind::Clearing, Role::Committed));
    check(!clearing.empty() && clearing.front().dashed == 1.0f &&
          !inside.empty() && inside.front().dashed == 0.0f,
          "a clearing's outline is dashed and a stand's is solid");
    check(near(VegetationGeometry::styleFor(VegetationKind::Stand, Role::Footprint).a, 0.4, 1e-6) &&
          !VegetationGeometry::styleFor(VegetationKind::Clearing, Role::Footprint).dashed,
          "the solver's footprint is dim and never dashed");
    check(near(VegetationGeometry::styleFor(VegetationKind::Clearing, Role::Preview).a, 0.85, 1e-6) &&
          VegetationGeometry::styleFor(VegetationKind::Clearing, Role::Preview).dashed,
          "a clearing being drawn previews dashed");
}

void testFromCornersAndCoversACell() {
    const glm::vec2 extent{500.0f, 400.0f};
    const Rect a = VegetationGeometry::fromCorners({120.0f, 80.0f}, {40.0f, 300.0f}, extent);
    const Rect b = VegetationGeometry::fromCorners({40.0f, 300.0f}, {120.0f, 80.0f}, extent);
    const Rect c = VegetationGeometry::fromCorners({40.0f, 80.0f}, {120.0f, 300.0f}, extent);
    const Rect d = VegetationGeometry::fromCorners({120.0f, 300.0f}, {40.0f, 80.0f}, extent);
    const auto same = [](const Rect& p, const Rect& q) {
        return p.minX == q.minX && p.minZ == q.minZ && p.maxX == q.maxX && p.maxZ == q.maxZ;
    };
    check(same(a, b) && same(a, c) && same(a, d) &&
          a.minX == 40.0f && a.maxX == 120.0f && a.minZ == 80.0f && a.maxZ == 300.0f,
          "a drag from any corner gives the same normalised rectangle");
    const Rect clamped = VegetationGeometry::fromCorners({-10.0f, 20.0f}, {600.0f, 5.0f}, extent);
    check(clamped.minX == 0.0f && clamped.maxX == 500.0f && clamped.minZ == 5.0f &&
          clamped.maxZ == 20.0f,
          "a drag past the terrain is clamped to it");

    const TerrainModel flat = TerrainLoader::makeFlat(16, 16, 10.0f, 0.0f);
    check(VegetationGeometry::coversACell({0.0f, 0.0f, 10.0f, 10.0f}, flat),
          "a rectangle one cell on a side covers a cell");
    check(!VegetationGeometry::coversACell({0.0f, 0.0f, 9.9f, 10.0f}, flat) &&
          !VegetationGeometry::coversACell({0.0f, 0.0f, 10.0f, 9.9f}, flat),
          "a rectangle short of a cell on either axis does not");
}

void testPickSmallest() {
    const std::vector<VegetationArea> areas = {
        {0.0f, 0.0f, 100.0f, 100.0f, 15.0f, "wood"},
        {40.0f, 40.0f, 60.0f, 60.0f, 10.0f, "copse"},
        {200.0f, 200.0f, 250.0f, 250.0f, 10.0f, "first twin"},
        {200.0f, 200.0f, 250.0f, 250.0f, 10.0f, "second twin"},
        {0.0f, 0.0f, 1000.0f, 1000.0f, 0.0f, "invalid, zero height"},
    };
    check(VegetationGeometry::pick(areas, 50.0f, 50.0f) == 1,
          "a click inside a copse inside a wood selects the copse");
    check(VegetationGeometry::pick(areas, 10.0f, 10.0f) == 0,
          "a click in the wood but outside the copse selects the wood");
    check(VegetationGeometry::pick(areas, 225.0f, 225.0f) == 3,
          "between areas of equal size the later one, drawn on top, is picked");
    check(VegetationGeometry::pick(areas, 500.0f, 500.0f) == -1,
          "an invalid area is never picked, even where nothing else is");
}

VegetationGeometry::ProjectedCorners corners(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d) {
    VegetationGeometry::ProjectedCorners projected;
    projected.px = {a, b, c, d};
    projected.visible = {true, true, true, true};
    return projected;
}

void testGripHitTest() {
    const Rect selected{100.0f, 100.0f, 300.0f, 250.0f};
    const auto wide = corners({100.0f, 100.0f}, {300.0f, 100.0f}, {300.0f, 250.0f}, {100.0f, 250.0f});

    check(VegetationGeometry::hitTest(selected, wide, {104.0f, 103.0f}, false, {}) == Grip::MinXMinZ,
          "a cursor near a corner grabs that corner");
    check(VegetationGeometry::hitTest(selected, wide, {200.0f, 175.0f}, true, {200.0f, 175.0f}) ==
              Grip::Body,
          "a cursor over the middle grabs the body");
    check(VegetationGeometry::hitTest(selected, wide, {297.0f, 248.0f}, true, {295.0f, 245.0f}) ==
              Grip::MaxXMaxZ,
          "a corner within reach beats the body beneath it");
    check(VegetationGeometry::hitTest(selected, wide, {600.0f, 600.0f}, false, {}) == Grip::None,
          "a cursor away from the area grabs nothing");

    // A small projected area: the shortest edge is 12 px, so the grab radius
    // shrinks to 4 px and a cursor 6 px from a corner reaches the body instead.
    const auto small = corners({100.0f, 100.0f}, {130.0f, 100.0f}, {130.0f, 112.0f}, {100.0f, 112.0f});
    check(VegetationGeometry::hitTest(selected, small, {106.0f, 100.0f}, true, {110.0f, 105.0f}) ==
              Grip::Body,
          "on a small area the grab radius shrinks so the body stays reachable");

    auto hidden = wide;
    hidden.visible[0] = false;
    check(VegetationGeometry::hitTest(selected, hidden, {101.0f, 101.0f}, false, {}) == Grip::None,
          "a corner behind the camera cannot be grabbed");
}

void testReshape() {
    const glm::vec2 extent{500.0f, 500.0f};
    const Rect original{100.0f, 100.0f, 200.0f, 150.0f};

    const Rect moved = VegetationGeometry::reshape(original, Grip::Body, {150.0f, 125.0f},
                                                   {480.0f, 125.0f}, extent);
    check(near(moved.maxX - moved.minX, 100.0, 1e-4) && near(moved.maxZ - moved.minZ, 50.0, 1e-4),
          "moving an area keeps its size");
    check(near(moved.maxX, 500.0, 1e-4) && near(moved.minX, 400.0, 1e-4),
          "an area moved past the edge stops at it rather than shrinking");

    const Rect resized = VegetationGeometry::reshape(original, Grip::MaxXMaxZ, {198.0f, 148.0f},
                                                     {298.0f, 248.0f}, extent);
    check(near(resized.minX, 100.0, 1e-4) && near(resized.minZ, 100.0, 1e-4),
          "resizing from a corner keeps the opposite corner fixed");
    check(near(resized.maxX, 300.0, 1e-4) && near(resized.maxZ, 250.0, 1e-4),
          "a corner grabbed just inside moves by the drag, not to the cursor");

    const Rect flipped = VegetationGeometry::reshape(original, Grip::MaxXMaxZ, {200.0f, 150.0f},
                                                     {50.0f, 60.0f}, extent);
    check(flipped.minX < flipped.maxX && flipped.minZ < flipped.maxZ &&
          near(flipped.minX, 50.0, 1e-4) && near(flipped.maxX, 100.0, 1e-4) &&
          near(flipped.minZ, 60.0, 1e-4) && near(flipped.maxZ, 100.0, 1e-4),
          "dragging a corner past its opposite re-normalises instead of inverting");

    const Rect untouched = VegetationGeometry::reshape(original, Grip::None, {0.0f, 0.0f},
                                                       {400.0f, 400.0f}, extent);
    check(untouched.minX == original.minX && untouched.maxZ == original.maxZ,
          "no grip changes nothing");
}

void testSnapshotCommandMerging() {
    std::vector<Antenna> sources;
    std::vector<VegetationArea> areas;
    int sourceSelection = -1;
    int areaSelection = -1;
    const auto applySources = [&](const std::vector<Antenna>& state, int selected) {
        sources = state;
        sourceSelection = selected;
    };
    const auto applyAreas = [&](const std::vector<VegetationArea>& state, int selected) {
        areas = state;
        areaSelection = selected;
    };
    using SourceCommand = SnapshotCommand<std::vector<Antenna>>;
    using AreaCommand = SnapshotCommand<std::vector<VegetationArea>>;

    QUndoStack merging;
    Antenna first;
    first.frequencyMHz = 100.0f;
    Antenna second;
    second.frequencyMHz = 200.0f;
    merging.push(new SourceCommand({}, -1, {first}, 0, "Edit source", 1000, applySources));
    merging.push(new SourceCommand({first}, 0, {second}, 0, "Edit source", 1000, applySources));
    check(merging.count() == 1 && sources.size() == 1 && sources[0].frequencyMHz == 200.0f,
          "two edits with the same id inside the window become one undo step");

    const VegetationArea stand{0.0f, 0.0f, 10.0f, 10.0f, 12.0f, "stand"};
    merging.push(new AreaCommand({}, -1, {stand}, 0, "Draw stand", 1000, applyAreas));
    check(merging.count() == 2,
          "a vegetation edit never merges into a source edit, even with the same id");
    merging.push(new AreaCommand({stand}, 0, {}, -1, "Delete vegetation area", -1, applyAreas));
    merging.push(new AreaCommand({}, -1, {stand}, 0, "Draw stand", -1, applyAreas));
    check(merging.count() == 4, "commands with no merge id never merge");

    QUndoStack history;
    sources.clear();
    areas.clear();
    Antenna a1;
    a1.frequencyMHz = 1.0f;
    Antenna a2;
    a2.frequencyMHz = 2.0f;
    const VegetationArea v1{0.0f, 0.0f, 10.0f, 10.0f, 5.0f, "one"};
    const VegetationArea v2{0.0f, 0.0f, 20.0f, 20.0f, 5.0f, "two"};
    history.push(new SourceCommand({}, -1, {a1}, 0, "source 1", -1, applySources));
    history.push(new AreaCommand({}, -1, {v1}, 0, "area 1", -1, applyAreas));
    history.push(new SourceCommand({a1}, 0, {a1, a2}, 1, "source 2", -1, applySources));
    history.push(new AreaCommand({v1}, 0, {v1, v2}, 1, "area 2", -1, applyAreas));
    check(sources.size() == 2 && areas.size() == 2, "each push applies its after-state");

    history.undo();
    check(areas.size() == 1 && sources.size() == 2, "the newest edit, to areas, undoes first");
    history.undo();
    check(areas.size() == 1 && sources.size() == 1, "then the source edit before it");
    history.undo();
    check(areas.empty() && sources.size() == 1, "then the first area edit");
    history.undo();
    check(areas.empty() && sources.empty(), "and finally the first source edit");

    for (int i = 0; i < 4; ++i) history.redo();
    check(sources.size() == 2 && areas.size() == 2 && sourceSelection == 1 && areaSelection == 1,
          "redoing a mixed history reproduces both states and their selections");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    testTriangulationMatchesIndices();
    testMeshHeightFollowsDiagonal();
    testOutlineLiesOnMesh();
    testOutlineClippingAndStyle();
    testFromCornersAndCoversACell();
    testPickSmallest();
    testGripHitTest();
    testReshape();
    testSnapshotCommandMerging();

    std::cout << (failures == 0 ? "All vegetation editing tests passed\n"
                                : std::to_string(failures) + " vegetation editing test(s) failed\n");
    return failures == 0 ? 0 : 1;
}
