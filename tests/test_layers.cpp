// Headless tests for the map layer model. No widgets and no GL context, so
// these run on every platform and in the default (non-GUI) configure.
#include "map/LayerStack.h"
#include "map/Hillshade.h"
#include "data/TerrainLoader.h"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}

void testDefaults() {
    LayerStack stack;
    check(stack.size() == 9, "stack has nine default layers");
    check(stack.at(0).id == "satellite", "satellite is the bottom layer");
    check(stack.at(8).id == "fresnel", "fresnel is the top layer");
    check(stack.at(0).visible, "satellite starts visible");
    check(!stack.find("usgs-topo")->visible, "topo starts hidden");
    check(!stack.find("hillshade")->visible, "hillshade starts hidden");
    check(stack.find("coverage")->visible, "coverage starts visible");
    check(!stack.find("los")->visible, "los starts hidden");
    check(stack.find("missing") == nullptr, "unknown id returns null");
}

void testVisibilityAndOpacity() {
    LayerStack stack;
    int changes = 0;
    stack.setOnChanged([&changes] { ++changes; });

    stack.setVisible("hillshade", true);
    check(stack.find("hillshade")->visible, "setVisible turns a layer on");
    check(changes == 1, "setVisible notifies once");
    stack.setVisible("hillshade", true);
    check(changes == 1, "setting the same value does not notify");

    stack.setOpacity("hillshade", 0.25f);
    check(std::abs(stack.find("hillshade")->opacity - 0.25f) < 1e-6f,
          "setOpacity stores the value");
    stack.setOpacity("hillshade", 5.0f);
    check(std::abs(stack.find("hillshade")->opacity - 1.0f) < 1e-6f,
          "opacity clamps to one");
    stack.setOpacity("hillshade", -1.0f);
    check(std::abs(stack.find("hillshade")->opacity) < 1e-6f,
          "opacity clamps to zero");

    const int before = changes;
    stack.setVisible("not-a-layer", true);
    stack.setOpacity("not-a-layer", 0.5f);
    check(changes == before, "an unknown id changes nothing and notifies nothing");
}

void testReorder() {
    LayerStack stack;
    stack.move("hillshade", 0);
    check(stack.at(0).id == "hillshade", "move places a layer at the requested index");
    check(stack.at(1).id == "satellite", "move shifts the displaced layer up");
    check(stack.size() == 9, "move does not change the layer count");
    stack.move("hillshade", 99);
    check(stack.at(8).id == "hillshade", "an out-of-range index clamps to the top");
}

void testVisibleOfKind() {
    LayerStack stack;
    stack.setVisible("usgs-topo", true);
    const auto rasters = stack.visibleOfKind(LayerKind::BaseRaster);
    check(rasters.size() == 2, "both visible base rasters are returned");
    check(rasters.size() == 2 && rasters[0].id == "satellite" &&
              rasters[1].id == "usgs-topo",
          "visibleOfKind preserves stack order");
    stack.setVisible("satellite", false);
    check(stack.visibleOfKind(LayerKind::BaseRaster).size() == 1,
          "hiding a layer removes it from the result");
    check(stack.visibleOfKind(LayerKind::TerrainDerived).empty(),
          "a kind with nothing visible returns empty");
}

void testScenarioRoundTrip() {
    LayerStack stack;
    stack.setVisible("hillshade", true);
    stack.setOpacity("hillshade", 0.3f);
    stack.move("hillshade", 0);

    LayerStack restored;
    restored.applyState(stack.state());
    check(restored.at(0).id == "hillshade", "applyState restores order");
    check(restored.find("hillshade")->visible, "applyState restores visibility");
    check(std::abs(restored.find("hillshade")->opacity - 0.3f) < 1e-6f,
          "applyState restores opacity");
    check(restored.size() == 9, "applyState keeps every layer");

    LayerStack unknown;
    unknown.applyState({{"not-a-layer", true, 1.0f}});
    check(unknown.size() == 9, "an unknown id is ignored, not inserted");
    check(unknown.at(0).id == "satellite", "an unknown id leaves order intact");

    // A partial list must place the named layers first and keep the rest.
    LayerStack partial;
    partial.applyState({{"fresnel", true, 0.5f}});
    check(partial.at(0).id == "fresnel", "a partial list orders named layers first");
    check(partial.size() == 9, "a partial list keeps unnamed layers");
    check(partial.at(1).id == "satellite", "unnamed layers keep their relative order");

    LayerStack empty;
    empty.applyState({});
    check(empty.find("satellite")->visible, "empty state leaves defaults alone");
    check(empty.at(0).id == "satellite", "empty state leaves order alone");
}

TerrainModel rampNorth(int samples, float cellSize) {
    TerrainModel t = TerrainLoader::makeFlat(samples, samples, cellSize, 0.0f);
    for (int z = 0; z < t.height; ++z)
        for (int x = 0; x < t.width; ++x)
            t.heights[z * t.width + x] = static_cast<float>(z) * 10.0f;
    return t;
}

void testHillshade() {
    const TerrainModel ramp = rampNorth(16, 30.0f);
    const QImage shaded = Hillshade::render(ramp, 315.0, 45.0);
    check(!shaded.isNull(), "hillshade produces an image");
    check(shaded.width() == ramp.width && shaded.height() == ramp.height,
          "hillshade matches the terrain grid size");

    const TerrainModel flat = TerrainLoader::makeFlat(16, 16, 30.0f, 0.0f);
    const QImage flatShaded = Hillshade::render(flat, 315.0, 45.0);
    check(qGray(shaded.pixel(8, 8)) != qGray(flatShaded.pixel(8, 8)),
          "a slope shades differently from flat ground");

    // Flat ground has zero slope everywhere, so every sample takes the same
    // value: cos(zenith). At 45 degrees altitude that is ~0.707 -> 180.
    check(std::abs(qGray(flatShaded.pixel(2, 2)) - 180) <= 1,
          "flat ground shades to cos(zenith)");
    check(qGray(flatShaded.pixel(2, 2)) == qGray(flatShaded.pixel(13, 13)),
          "flat ground shades uniformly");

    // Shading must come from true elevations and real spacing: widening the
    // samples makes the same height change a gentler real slope.
    const QImage wide = Hillshade::render(rampNorth(16, 60.0f), 315.0, 45.0);
    check(qGray(wide.pixel(8, 8)) != qGray(shaded.pixel(8, 8)),
          "sample spacing changes the computed slope");

    // Moving the light to the opposite bearing must change a sloped sample.
    const QImage fromSoutheast = Hillshade::render(ramp, 135.0, 45.0);
    check(qGray(fromSoutheast.pixel(8, 8)) != qGray(shaded.pixel(8, 8)),
          "illumination azimuth changes the result");

    check(Hillshade::render(TerrainLoader::makeFlat(1, 1, 30.0f, 0.0f), 315.0, 45.0).isNull(),
          "a grid smaller than 2x2 returns a null image");
}

} // namespace

int main() {
    testDefaults();
    testVisibilityAndOpacity();
    testReorder();
    testVisibleOfKind();
    testScenarioRoundTrip();
    testHillshade();
    if (failures == 0) std::cout << "Layer tests passed.\n";
    return failures == 0 ? 0 : 1;
}
