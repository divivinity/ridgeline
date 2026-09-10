// Headless tests for the map layer model. No widgets and no GL context, so
// these run on every platform and in the default (non-GUI) configure.
#include "map/LayerStack.h"
#include "map/Hillshade.h"
#include "map/TileProviders.h"
#include "map/MapMeasure.h"
#include "ui/TerrainPresentation.h"

#include <QColor>
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
    stack.addListener([&changes] { ++changes; });

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

void testSourceResolutionReporting() {
    // Grid spacing and source resolution are different things, and the tooltip
    // is the only place a user can tell interpolation from real detail.
    TerrainModel coarseSource = TerrainLoader::makeFlat(64, 64, 5.0f, 0.0f);
    coarseSource.sourceResolutionM = 30.0f;
    const QString upsampled = TerrainPresentation::gridDetails(coarseSource);
    check(upsampled.contains("Source resolution: about 30.0 m"),
          "the source resolution is reported");
    check(upsampled.contains("interpolated"),
          "a grid finer than its source says the extra samples are interpolated");

    TerrainModel fineSource = TerrainLoader::makeFlat(64, 64, 90.0f, 0.0f);
    fineSource.sourceResolutionM = 30.0f;
    check(TerrainPresentation::gridDetails(fineSource).contains("some detail is not shown"),
          "a grid coarser than its source says detail is lost");

    TerrainModel matched = TerrainLoader::makeFlat(64, 64, 30.0f, 0.0f);
    matched.sourceResolutionM = 30.0f;
    const QString even = TerrainPresentation::gridDetails(matched);
    check(even.contains("Source resolution: about 30.0 m"), "a matched grid still reports it");
    check(!even.contains("interpolated") && !even.contains("not shown"),
          "a grid matching its source claims neither loss nor invention");

    TerrainModel unknown = TerrainLoader::makeFlat(64, 64, 30.0f, 0.0f);
    check(TerrainPresentation::gridDetails(unknown).contains("not known"),
          "an unknown source resolution says so rather than guessing");
}

void testProviders() {
    const MapTileProvider topo = TileProviders::usgsTopo();
    check(topo.id == "usgs-topo", "usgs provider carries the layer id");
    check(topo.usOnly, "usgs topo is flagged US-only");
    check(topo.attribution.contains("USGS"), "usgs provider credits the USGS");
    check(topo.maxZoom == 16, "usgs topo declares its zoom ceiling");

    // ArcGIS addresses tiles row before column. Pin the produced URL so a
    // transposed template, which would silently mirror a whole basemap, fails
    // here instead of on screen.
    check(TileProviders::tileUrl(topo, 5, 7, 22).endsWith("/5/22/7"),
          "usgs url puts row before column");
    const MapTileProvider esri = TileProviders::esriImagery();
    check(TileProviders::tileUrl(esri, 5, 7, 22).endsWith("/5/22/7"),
          "esri url puts row before column");
    check(!esri.usOnly, "esri imagery is not US-only");

    check(TileProviders::cachePath(topo, 5, 7, 22) == "imagery/usgs-topo/5/7/22.tile",
          "cache path uses the provider namespace and logical x/y");
    check(TileProviders::cachePath(esri, 5, 7, 22) != TileProviders::cachePath(topo, 5, 7, 22),
          "two providers never share a cache path");

    // USGS answers out-of-coverage requests with HTTP 200 and a uniform fill,
    // so a successful download is not evidence that a map arrived. Verified
    // against the live service: an Atlantic tile returns a flat pale blue.
    QImage blank(256, 256, QImage::Format_RGBA8888);
    blank.fill(QColor(186, 226, 253));
    check(TileProviders::isFeatureless(blank), "a uniform no-data tile is featureless");

    QImage jpegish = blank;
    for (int y = 0; y < jpegish.height(); ++y)
        for (int x = 0; x < jpegish.width(); ++x)
            jpegish.setPixel(x, y, qRgb(186 + ((x + y) % 3), 226, 253));
    check(TileProviders::isFeatureless(jpegish),
          "slight compression noise across a flat fill is still featureless");

    QImage cartography = blank;
    for (int x = 0; x < cartography.width(); ++x)
        cartography.setPixel(x, 128, qRgb(120, 90, 40));   // a contour line
    check(!TileProviders::isFeatureless(cartography),
          "a tile carrying any cartography is not featureless");
    check(TileProviders::isFeatureless(QImage()), "a null image is featureless");

    MapTileProvider found;
    check(TileProviders::forLayer("usgs-topo", found) && found.id == "usgs-topo",
          "forLayer resolves a raster layer");
    check(!TileProviders::forLayer("hillshade", found),
          "a terrain-derived layer has no tile provider");
    check(!TileProviders::forLayer("coverage", found),
          "an analysis layer has no tile provider");
}


void testMeasurement() {
    // Asheville NC to Charlotte NC: about 160 km on a bearing near east-southeast.
    const double d = MapMeasure::haversineMetres(35.5951, -82.5515, 35.2271, -80.8431);
    check(d > 155000.0 && d < 165000.0, "great-circle distance matches a known pair");
    const double b = MapMeasure::initialBearingDeg(35.5951, -82.5515, 35.2271, -80.8431);
    check(b > 100.0 && b < 115.0, "initial bearing points east-southeast");

    // One degree of latitude is close to 111 km anywhere.
    const double degree = MapMeasure::haversineMetres(0.0, 0.0, 1.0, 0.0);
    check(std::abs(degree - 111195.0) < 400.0, "one degree of latitude is about 111 km");
    check(std::abs(MapMeasure::haversineMetres(10.0, 20.0, 10.0, 20.0)) < 1e-6,
          "a point is zero distance from itself");

    // A path across the antimeridian must take the short way round.
    const double wrapped = MapMeasure::haversineMetres(0.0, 179.5, 0.0, -179.5);
    check(wrapped > 100000.0 && wrapped < 120000.0,
          "an antimeridian crossing measures the short way round");

    check(std::abs(MapMeasure::initialBearingDeg(0.0, 0.0, 1.0, 0.0)) < 1e-6,
          "due north is bearing zero");
    check(std::abs(MapMeasure::initialBearingDeg(0.0, 0.0, 0.0, 1.0) - 90.0) < 1e-6,
          "due east is bearing ninety");
    check(std::abs(MapMeasure::initialBearingDeg(0.0, 0.0, -1.0, 0.0) - 180.0) < 1e-6,
          "due south is bearing one eighty");

    check(std::abs(MapMeasure::planarMetres(0, 0, 3, 4) - 5.0) < 1e-9,
          "planar distance is euclidean");

    check(MapMeasure::compassPoint(0) == "N", "zero degrees is north");
    check(MapMeasure::compassPoint(45) == "NE", "forty five degrees is north east");
    check(MapMeasure::compassPoint(359) == "N", "just short of a full turn is north");
    check(MapMeasure::compassPoint(-90) == "W", "a negative bearing wraps");
}

void testScaleBar() {
    // 10 m per pixel over 200 px is 2000 m, so the bar should read 2 km.
    const auto bar = MapMeasure::chooseScaleBar(10.0, 200.0);
    check(bar.valid(), "a positive scale produces a bar");
    check(std::abs(bar.metres - 2000.0) < 1e-9, "the bar takes a round 1/2/5 value");
    check(std::abs(bar.pixels - 200.0) < 1e-9, "the bar is drawn at its true width");
    check(bar.label == "2.00 km", "the bar is labelled in kilometres");
    check(bar.pixels <= 200.0, "the bar never exceeds the space allowed");

    for (double scale : {0.05, 0.5, 3.0, 42.0, 1234.0}) {
        const auto b = MapMeasure::chooseScaleBar(scale, 180.0);
        check(b.valid() && b.pixels <= 180.0,
              "every scale yields a bar that fits the space allowed");
        check(!b.label.isEmpty(), "every bar carries a label");
    }

    check(!MapMeasure::chooseScaleBar(0.0, 200.0).valid(), "a zero scale has no bar");
    check(!MapMeasure::chooseScaleBar(-1.0, 200.0).valid(), "a negative scale has no bar");
    check(!MapMeasure::chooseScaleBar(10.0, 0.0).valid(), "no space means no bar");

    check(MapMeasure::formatDistance(0.5) == "0.50 m", "sub-metre keeps two decimals");
    check(MapMeasure::formatDistance(250.0) == "250 m", "metres round to whole numbers");
    check(MapMeasure::formatDistance(15000.0) == "15.0 km", "long distances use kilometres");
    check(MapMeasure::formatDistance(-1.0) == "unavailable", "a negative distance is unavailable");

    // A hair below datum must read as zero, not as a signed zero.
    check(MapMeasure::withoutNegativeZero(-0.004) == 0.0, "a tiny negative rounds to plain zero");
    check(!std::signbit(MapMeasure::withoutNegativeZero(-0.004)), "the result is not negative zero");
    check(MapMeasure::withoutNegativeZero(-0.4, 1) == -0.4, "a real negative is preserved");
    check(MapMeasure::withoutNegativeZero(2.6) == 3.0, "rounding still applies");
}

} // namespace

int main() {
    testDefaults();
    testVisibilityAndOpacity();
    testReorder();
    testVisibleOfKind();
    testScenarioRoundTrip();
    testHillshade();
    testSourceResolutionReporting();
    testProviders();
    testMeasurement();
    testScaleBar();
    if (failures == 0) std::cout << "Layer tests passed.\n";
    return failures == 0 ? 0 : 1;
}
