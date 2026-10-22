// Headless tests for canopy ingestion: the TIFF reader, the LANDFIRE decode,
// the geographic resample, and the compositing of drawn stands over imported
// data. No widgets, no GL context and no network - the one real provider
// response the suite needs is committed under tests/data/canopy.
#include "data/CanopyRaster.h"
#include "CanopyTiffFixture.h"
#include "data/LandfireCanopy.h"
#include "data/RasterTiff.h"
#include "data/TerrainLoader.h"
#include "sim/SolveSignature.h"
#include "sim/TerrainModel.h"
#include "sim/VegetationAreas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
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

// ---------------------------------------------------------------------------
// A TIFF writer, so the reader is tested against bytes this suite controls
// rather than only against one provider's output.
// ---------------------------------------------------------------------------


TiffBuilder rampBuilder(int w, int h) {
    TiffBuilder b;
    b.width = w;
    b.height = h;
    b.samples.resize(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            b.samples[static_cast<std::size_t>(y) * w + x] = y * 100 + x;
    return b;
}

bool matchesRamp(const RasterTiff& raster, int w, int h) {
    if (raster.width != w || raster.height != h) return false;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (!near(raster.at(x, y), y * 100 + x, 1e-6)) return false;
    return true;
}

std::string decodeError(const std::vector<std::uint8_t>& bytes) {
    try {
        RasterTiff::decode(bytes);
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

bool mentions(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------

void testTiffLayouts() {
    TiffBuilder stripped = rampBuilder(7, 5);
    check(matchesRamp(RasterTiff::decode(stripped.build()), 7, 5),
          "a single-strip little-endian signed 16-bit raster decodes");

    TiffBuilder multiStrip = rampBuilder(7, 5);
    multiStrip.rowsPerStrip = 2;
    check(matchesRamp(RasterTiff::decode(multiStrip.build()), 7, 5),
          "a multi-strip raster decodes, with each strip placed at its own row");

    TiffBuilder bigEndian = rampBuilder(7, 5);
    bigEndian.littleEndian = false;
    check(matchesRamp(RasterTiff::decode(bigEndian.build()), 7, 5),
          "a big-endian raster decodes");

    TiffBuilder tiled = rampBuilder(7, 5);
    tiled.tiled = true;
    tiled.tileSize = 4;
    check(matchesRamp(RasterTiff::decode(tiled.build()), 7, 5),
          "a tiled raster decodes, ignoring the padding past the image edge");

    TiffBuilder bigEndianTiled = rampBuilder(9, 9);
    bigEndianTiled.littleEndian = false;
    bigEndianTiled.tiled = true;
    bigEndianTiled.tileSize = 4;
    check(matchesRamp(RasterTiff::decode(bigEndianTiled.build()), 9, 9),
          "a big-endian tiled raster with several tiles across decodes");
}

void testTiffSampleFormats() {
    TiffBuilder u8 = rampBuilder(3, 2);
    u8.bitsPerSample = 8;
    u8.sampleFormat = 1;
    for (auto& v : u8.samples) v = std::fmod(v, 200.0);
    const RasterTiff decoded8 = RasterTiff::decode(u8.build());
    check(near(decoded8.at(2, 1), 102.0, 1e-9), "unsigned 8-bit samples decode");

    TiffBuilder s16 = rampBuilder(3, 2);
    s16.samples[0] = -9999.0;
    const RasterTiff decoded16 = RasterTiff::decode(s16.build());
    check(near(decoded16.at(0, 0), -9999.0, 1e-9),
          "signed 16-bit samples keep their sign, so no-data survives");

    TiffBuilder u16 = rampBuilder(3, 2);
    u16.sampleFormat = 1;
    u16.samples[0] = 40000.0;
    check(near(RasterTiff::decode(u16.build()).at(0, 0), 40000.0, 1e-9),
          "unsigned 16-bit samples above the signed range decode");

    TiffBuilder s32 = rampBuilder(3, 2);
    s32.bitsPerSample = 32;
    s32.samples[1] = -70000.0;
    check(near(RasterTiff::decode(s32.build()).at(1, 0), -70000.0, 1e-9),
          "signed 32-bit samples decode");

    TiffBuilder f32 = rampBuilder(3, 2);
    f32.bitsPerSample = 32;
    f32.sampleFormat = 3;
    f32.samples[4] = 12.5;
    check(near(RasterTiff::decode(f32.build()).at(1, 1), 12.5, 1e-6),
          "32-bit float samples decode");
}

void testTiffRejections() {
    TiffBuilder bigTiff = rampBuilder(3, 2);
    bigTiff.magic = 43;
    check(mentions(decodeError(bigTiff.build()), "BigTIFF"),
          "a BigTIFF is refused by name rather than mis-read");

    TiffBuilder compressed = rampBuilder(3, 2);
    compressed.compression = 8;
    check(mentions(decodeError(compressed.build()), "compression 8"),
          "an unsupported compression is refused, naming the code");

    TiffBuilder rgb = rampBuilder(3, 2);
    rgb.samplesPerPixel = 3;
    check(mentions(decodeError(rgb.build()), "samples per pixel"),
          "a multi-band raster is refused");

    std::vector<std::uint8_t> notTiff = {'P', 'K', 3, 4, 0, 0, 0, 0};
    check(mentions(decodeError(notTiff), "byte-order mark"),
          "a file that is not a TIFF at all is refused");

    check(mentions(decodeError({'I', 'I'}), "too short"),
          "a truncated header is refused before anything is read from it");

    TiffBuilder truncated = rampBuilder(4, 4);
    std::vector<std::uint8_t> bytes = truncated.build();
    bytes.resize(bytes.size() - 10);
    check(!decodeError(bytes).empty(),
          "a file whose pixel data is cut short is refused, not padded");
}

void testTiffGeoreferencing() {
    TiffBuilder geo = rampBuilder(4, 4);
    geo.geo = true;
    geo.westLon = -78.5;
    geo.northLat = 35.25;
    geo.step = 0.001;
    const RasterTiff decoded = RasterTiff::decode(geo.build());
    check(decoded.georeferenced, "a pixel scale and tiepoint are read");
    check(near(decoded.westLonDeg, -78.5, 1e-12) &&
          near(decoded.northLatDeg, 35.25, 1e-12) &&
          near(decoded.lonStepDeg, 0.001, 1e-12),
          "the tiepoint places the north-west corner and the scale sizes a pixel");

    const RasterTiff plain = RasterTiff::decode(rampBuilder(4, 4).build());
    check(!plain.georeferenced,
          "a raster with no placement tags reports that it has none");
}

// ---------------------------------------------------------------------------
// LANDFIRE
// ---------------------------------------------------------------------------

TerrainModel georeferencedTerrain(int samples, double centerLat, double centerLon,
                                  double spanDeg) {
    TerrainModel terrain = TerrainLoader::makeFlat(samples, samples, 30.0f, 100.0f);
    const double step = spanDeg / (samples - 1);
    terrain.setGeoReference(centerLat - spanDeg * 0.5, centerLon - spanDeg * 0.5,
                            step, step);
    return terrain;
}

void testRegionAndUrl() {
    check(landfire::regionFor(35.05, -78.88) == landfire::Region::Conus,
          "North Carolina resolves to the CONUS mosaic");
    check(landfire::regionFor(61.2, -149.9) == landfire::Region::Alaska,
          "Anchorage resolves to the Alaska mosaic");
    check(landfire::regionFor(21.3, -157.8) == landfire::Region::Hawaii,
          "Honolulu resolves to the Hawaii mosaic");
    check(landfire::regionFor(18.4, -66.1) == landfire::Region::PuertoRicoVI,
          "San Juan resolves to the Puerto Rico mosaic");
    check(landfire::regionFor(48.85, 2.35) == landfire::Region::None,
          "Paris is outside LANDFIRE, and says so rather than guessing a mosaic");
    check(landfire::regionFor(std::nan(""), 0.0) == landfire::Region::None,
          "a non-finite position resolves to no region");

    landfire::Bounds bounds{-78.9, 35.0, -78.8, 35.1};
    const std::string url = landfire::exportImageUrl(
        "LF2024", landfire::Region::Conus, landfire::Layer::CanopyHeight,
        bounds, 128, 128);
    check(mentions(url, "Landfire_LF2024/LF2024_CH_CONUS/ImageServer/exportImage"),
          "the URL names the product, layer and region service");
    check(mentions(url, "bbox=-78.9,35.0,-78.8,35.1"),
          "the bounding box is written at full precision without a long tail of "
          "trailing zeroes");
    check(mentions(url, "size=128,128") && mentions(url, "bboxSR=4326") &&
          mentions(url, "imageSR=4326") && mentions(url, "format=tiff"),
          "the request asks for a WGS84 TIFF at the requested size");
    check(mentions(url, "interpolation=RSP_NearestNeighbor"),
          "resampling is nearest neighbour, so class codes are not averaged");

    const std::string overridden = landfire::exportImageUrl(
        "LF2024", landfire::Region::Conus, landfire::Layer::CanopyCover,
        bounds, 64, 32, "file:///cache/{layer}-{region}-{width}x{height}.tif");
    check(overridden == "file:///cache/CC-CONUS-64x32.tif",
          "an override template substitutes placeholders by name");
}

void testBoundsCoverSampleCentres() {
    const TerrainModel terrain = georeferencedTerrain(5, 35.0, -78.0, 0.04);
    const landfire::Bounds bounds = landfire::boundsForTerrain(terrain);
    const double step = 0.04 / 4;

    check(near(bounds.west, -78.02 - step * 0.5, 1e-12) &&
          near(bounds.east, -77.98 + step * 0.5, 1e-12),
          "the requested box is grown by half a cell, so pixel centres land on "
          "terrain samples");

    // The property that matters: the centre of pixel i is terrain sample i.
    const double lonStep = (bounds.east - bounds.west) / terrain.width;
    bool aligned = true;
    for (int x = 0; x < terrain.width; ++x) {
        const double centre = bounds.west + (x + 0.5) * lonStep;
        const double sample = terrain.originLonDeg + x * terrain.cellLonDeg;
        if (!near(centre, sample, 1e-12)) aligned = false;
    }
    check(aligned, "every pixel centre coincides with a terrain sample");

    TerrainModel plain = TerrainLoader::makeFlat(8, 8, 30.0f);
    check(!landfire::boundsForTerrain(plain).valid(),
          "terrain with no geographic reference yields no request box");
}

// Three tiny rasters standing in for a provider response, with a patch of
// developed vegetation in a corner.
struct FakeResponse {
    RasterTiff height, cover, vegetation;
    landfire::Bounds bounds{-78.02, 34.98, -77.98, 35.02};
};

FakeResponse fakeResponse() {
    FakeResponse r;
    const int n = 4;
    auto make = [&](std::vector<double> values) {
        TiffBuilder b = rampBuilder(n, n);
        b.samples = std::move(values);
        b.geo = true;
        b.westLon = r.bounds.west;
        b.northLat = r.bounds.north;
        b.step = (r.bounds.east - r.bounds.west) / n;
        return RasterTiff::decode(b.build());
    };
    // Row 0: natural forest. Row 1: developed evergreen (LANDFIRE's 390/45
    // constant). Row 2: bare. Row 3: no data.
    r.height = make({230, 230, 230, 230,
                     390, 390, 390, 390,
                     0,   0,   0,   0,
                     -9999, -9999, -9999, -9999});
    r.cover  = make({75, 75, 75, 75,
                     45, 45, 45, 45,
                     0,  0,  0,  0,
                     -9999, -9999, -9999, -9999});
    r.vegetation = make({123, 123, 123, 123,
                         14,  14,  14,  14,
                         25,  25,  25,  25,
                         -9999, -9999, -9999, -9999});
    return r;
}

void testAssemble() {
    const FakeResponse r = fakeResponse();

    landfire::Options assume;    // the default: substitute a stated assumption
    const CanopyRaster assumed = landfire::assemble(
        r.height, r.cover, r.vegetation, r.bounds, assume, "LF2024", "2026-09-11T00:00:00Z");

    check(assumed.width == 4 && assumed.height == 4, "the canopy keeps the raster size");
    check(near(assumed.northLatDeg, r.bounds.north, 1e-12) &&
          near(assumed.westLonDeg, r.bounds.west, 1e-12),
          "the canopy is anchored to the box that was requested");
    check(assumed.coverage.samples == 16, "every cell is counted as a sample");
    check(assumed.coverage.withData == 12,
          "the no-data row is excluded from the cells the provider answered for");
    check(assumed.coverage.vegetated == 8,
          "only the forested and developed rows count as vegetated");
    check(assumed.coverage.assignedHeight == 4,
          "the developed row is counted as an assigned height, not a measurement");

    check(assumed.heightDm[0] == 230, "measured canopy height passes through");
    check(assumed.heightDm[4] == 100,
          "the developed row is replaced by the ten-metre assumption");
    check(assumed.coverPct[0] == 75 && assumed.coverPct[4] == 45,
          "cover comes through unchanged, including over developed land");
    check(assumed.heightDm[8] == 0 && assumed.heightDm[12] == 0,
          "bare ground and no-data both end up with no canopy");

    landfire::Options raw;
    raw.developed = landfire::DevelopedPolicy::UseSourceValue;
    const CanopyRaster passthrough = landfire::assemble(
        r.height, r.cover, r.vegetation, r.bounds, raw, "LF2024", "");
    check(passthrough.heightDm[4] == 390,
          "asking for the source value passes LANDFIRE's constant through");
    check(passthrough.coverage.assignedHeight == 4,
          "passing the constant through still reports that it was assigned");

    landfire::Options bare;
    bare.developed = landfire::DevelopedPolicy::TreatAsBare;
    const CanopyRaster dropped = landfire::assemble(
        r.height, r.cover, r.vegetation, r.bounds, bare, "LF2024", "");
    check(dropped.heightDm[4] == 0, "asking to drop developed vegetation drops it");
    check(dropped.coverage.vegetated == 4 && dropped.coverage.assignedHeight == 0,
          "dropped cells count as neither vegetated nor assigned");

    landfire::Options tall;
    tall.assumedDevelopedCanopyM = 6.5f;
    check(landfire::assemble(r.height, r.cover, r.vegetation, r.bounds, tall,
                             "LF2024", "").heightDm[4] == 65,
          "the assumed height is the one that was asked for");

    check(assumed.provenance.licence.find("Public domain") != std::string::npos &&
          !assumed.provenance.attribution.empty(),
          "the layer carries its licence and credit line");
    check(assumed.provenance.productVintage == "LANDFIRE LF2024",
          "the layer records which release it came from");
}

void testAssembleRejections() {
    const FakeResponse r = fakeResponse();

    TiffBuilder wrongSize = rampBuilder(3, 3);
    const RasterTiff small = RasterTiff::decode(wrongSize.build());
    bool refusedSize = false;
    try {
        landfire::assemble(r.height, small, r.vegetation, r.bounds, {}, "LF2024", "");
    } catch (const std::exception& e) {
        refusedSize = mentions(e.what(), "different sizes");
    }
    check(refusedSize, "rasters of different sizes are refused");

    landfire::Bounds moved = r.bounds;
    moved.west  += 0.5;
    moved.east  += 0.5;
    bool refusedPlacement = false;
    try {
        landfire::assemble(r.height, r.cover, r.vegetation, moved, {}, "LF2024", "");
    } catch (const std::exception& e) {
        refusedPlacement = mentions(e.what(), "different area");
    }
    check(refusedPlacement,
          "a response covering somewhere other than the requested box is refused");

    check(landfire::isDevelopedVegetation(13) && landfire::isDevelopedVegetation(17),
          "the developed vegetation classes are 13 through 17");
    check(!landfire::isDevelopedVegetation(12) && !landfire::isDevelopedVegetation(18) &&
          !landfire::isDevelopedVegetation(123),
          "neighbouring classes and tree heights are not treated as developed");
}

// ---------------------------------------------------------------------------
// Placing a canopy layer on a terrain grid
// ---------------------------------------------------------------------------

CanopyRaster syntheticCanopy(double northLat, double westLon, double step,
                             int n, std::uint16_t heightDm, std::uint8_t coverPct) {
    CanopyRaster canopy;
    canopy.width = canopy.height = n;
    canopy.northLatDeg = northLat;
    canopy.westLonDeg = westLon;
    canopy.latStepDeg = canopy.lonStepDeg = step;
    canopy.heightDm.assign(static_cast<std::size_t>(n) * n, heightDm);
    canopy.coverPct.assign(static_cast<std::size_t>(n) * n, coverPct);
    canopy.coverage.samples = n * n;
    canopy.coverage.withData = n * n;
    canopy.coverage.vegetated = n * n;
    return canopy;
}

void testSampleOnto() {
    // A canopy covering 35.0 +/- 0.02, -78.0 +/- 0.02, at twice the terrain's
    // resolution, with a distinctive north-west corner.
    CanopyRaster canopy = syntheticCanopy(35.02, -78.02, 0.005, 8, 200, 60);
    canopy.heightDm[0] = 500;          // northernmost, westernmost sample

    TerrainModel terrain = georeferencedTerrain(5, 35.0, -78.0, 0.04);
    std::vector<std::uint16_t> heights;
    std::vector<std::uint8_t> covers;
    check(canopy.sampleOnto(terrain, heights, covers),
          "a canopy covering the terrain resamples onto it");
    check(heights.size() == terrain.heights.size() &&
          covers.size() == terrain.heights.size(),
          "the resampled grids match the terrain sample for sample");

    // Terrain row 0 is its south edge; the canopy's row 0 is its north. The
    // tall corner must land in the north-west of the terrain, which is the last
    // row, first column.
    const std::size_t northWest =
        static_cast<std::size_t>(terrain.height - 1) * terrain.width;
    check(heights[northWest] == 500,
          "the canopy's northern row lands on the terrain's northern edge, "
          "not its southern one");
    check(heights[0] == 200, "the rest of the canopy resamples at its own value");
    check(covers[0] == 60, "cover resamples alongside height");

    CanopyRaster elsewhere = syntheticCanopy(48.9, 2.3, 0.005, 8, 200, 60);
    std::vector<std::uint16_t> noHeights;
    std::vector<std::uint8_t> noCovers;
    check(!elsewhere.sampleOnto(terrain, noHeights, noCovers),
          "a canopy describing different ground refuses to be installed");
    check(noHeights.empty(),
          "a refused resample leaves the outputs untouched rather than zeroed");

    TerrainModel plain = TerrainLoader::makeFlat(8, 8, 30.0f);
    check(!canopy.sampleOnto(plain, noHeights, noCovers),
          "terrain with no geographic reference cannot receive a canopy layer");

    CanopyRaster heightOnly = syntheticCanopy(35.02, -78.02, 0.005, 8, 200, 60);
    heightOnly.coverPct.clear();
    std::vector<std::uint16_t> h2;
    std::vector<std::uint8_t> c2;
    check(heightOnly.sampleOnto(terrain, h2, c2) && !h2.empty() && c2.empty(),
          "a layer with no cover measurement resamples heights and leaves cover "
          "empty, which reads as unknown");

    CanopyRaster allBare = syntheticCanopy(35.02, -78.02, 0.005, 8, 0, 0);
    allBare.coverage.vegetated = 0;
    check(!allBare.valid(),
          "an all-zero canopy is not valid data: it is an area with no coverage");
}

// ---------------------------------------------------------------------------
// Compositing and the solver's reading of cover
// ---------------------------------------------------------------------------

void testComposite() {
    TerrainModel terrain = TerrainLoader::makeFlat(8, 8, 10.0f, 0.0f);
    const std::size_t cells = terrain.heights.size();

    VegetationAreas::apply(terrain, {});
    check(!terrain.hasVegetation() && terrain.canopyCoverPct.empty(),
          "no import and nothing drawn leaves no vegetation at all");

    std::vector<std::uint16_t> base(cells, 180);
    std::vector<std::uint8_t> cover(cells, 40);
    VegetationAreas::apply(terrain, {}, base, cover);
    check(terrain.hasVegetation() && terrain.canopyHeightDm[0] == 180,
          "an imported layer alone populates the canopy");
    check(terrain.canopyCoverPct.size() == cells && terrain.canopyCoverPct[0] == 40,
          "imported cover is carried onto the terrain");

    VegetationAreas::apply(terrain, {{0.0f, 0.0f, 20.0f, 20.0f, 5.0f, "copse"}},
                           base, cover);
    check(terrain.canopyHeightDm[0] == 50,
          "a drawn stand replaces the imported height even when it is shorter");
    check(terrain.canopyCoverPct[0] == 100,
          "a drawn stand is complete cover, because it carries no measurement");
    const std::size_t farCorner = cells - 1;
    check(terrain.canopyHeightDm[farCorner] == 180,
          "ground outside the drawn stand keeps the imported height");

    VegetationAreas::apply(terrain, {{0.0f, 0.0f, 20.0f, 20.0f, 5.0f, "copse"},
                                     {10.0f, 10.0f, 30.0f, 30.0f, 9.0f, "wood"}},
                           base, cover);
    check(terrain.canopyHeightDm[terrain.width + 1] == 90,
          "where drawn stands overlap the tallest wins, not the sum");

    VegetationAreas::apply(terrain, {{0.0f, 0.0f, 20.0f, 20.0f, 5.0f, "copse"}});
    check(terrain.hasVegetation() && terrain.canopyCoverPct.empty(),
          "drawn stands with no import carry no cover grid, so cover reads as "
          "unknown and the loss is unchanged from before cover existed");

    std::vector<std::uint16_t> wrongSize(cells + 3, 200);
    VegetationAreas::apply(terrain, {}, wrongSize, {});
    check(!terrain.hasVegetation(),
          "a base sized for a different grid is refused rather than misplaced");

    VegetationAreas::apply(terrain, {{500.0f, 500.0f, 600.0f, 600.0f, 12.0f, "off"}});
    check(!terrain.hasVegetation(),
          "a stand entirely off the terrain leaves no vegetation behind");

    std::vector<std::uint8_t> fullCover(cells, 100);
    VegetationAreas::apply(terrain, {}, base, fullCover);
    check(terrain.hasVegetation() && terrain.canopyCoverPct.empty(),
          "a cover grid that is complete everywhere is dropped rather than "
          "carried, since it changes no answer");
}

void testCoverScalesDepth() {
    TerrainModel terrain = TerrainLoader::makeFlat(64, 64, 10.0f, 0.0f);
    const std::size_t cells = terrain.heights.size();
    std::vector<std::uint16_t> base(cells, 300);      // 30 m canopy everywhere

    VegetationAreas::apply(terrain, {}, base, {});
    const glm::vec3 from{5.0f, 15.0f, 5.0f};
    const glm::vec3 to{600.0f, 15.0f, 5.0f};
    const float fullDepth = terrain.foliageDepthM(from, to, 256);
    check(fullDepth > 500.0f,
          "a path inside a continuous canopy accumulates most of its length");

    std::vector<std::uint8_t> half(cells, 50);
    VegetationAreas::apply(terrain, {}, base, half);
    const float halfDepth = terrain.foliageDepthM(from, to, 256);
    check(near(halfDepth, fullDepth * 0.5, fullDepth * 0.01),
          "halving the crown cover halves the effective depth");

    check(near(terrain.canopyCoverAt(3, 3), 0.5f, 1e-6f),
          "cover is reported as a fraction");
    TerrainModel noCover = TerrainLoader::makeFlat(8, 8, 10.0f, 0.0f);
    check(near(noCover.canopyCoverAt(1, 1), 1.0f, 1e-6f),
          "terrain with no cover grid reports complete cover");

    // The line-of-sight walk must agree with the standalone measurement, or a
    // solver branch would disagree with the analysis panel.
    float viaLos = 0.0f;
    terrain.lineOfSightStatus(from, to, 256, &viaLos);
    check(near(viaLos, halfDepth, std::max(1.0f, halfDepth * 0.02f)),
          "the depth measured during the line-of-sight walk matches the "
          "standalone measurement");
}

void testSignatureSeesCover() {
    TerrainModel terrain = TerrainLoader::makeFlat(16, 16, 10.0f, 0.0f);
    const std::size_t cells = terrain.heights.size();
    std::vector<std::uint16_t> base(cells, 200);

    const std::vector<Antenna> antennas;
    const ReceiverSettings receiver;
    const CoverageCalibrationSettings calibration;
    auto sign = [&] {
        return SolveSignature::of(antennas, receiver, SimEngine::Mode::Planner,
                                  calibration, terrain);
    };

    VegetationAreas::apply(terrain, {}, base, std::vector<std::uint8_t>(cells, 80));
    const std::string dense = sign();

    VegetationAreas::apply(terrain, {}, base, std::vector<std::uint8_t>(cells, 30));
    check(sign() != dense,
          "changing only the crown cover changes the solve fingerprint");

    VegetationAreas::apply(terrain, {}, base, std::vector<std::uint8_t>(cells, 80));
    check(sign() == dense,
          "restoring the same cover restores the same fingerprint");

    std::vector<std::uint16_t> taller(cells, 240);
    VegetationAreas::apply(terrain, {}, taller, std::vector<std::uint8_t>(cells, 80));
    check(sign() != dense,
          "changing only the canopy height changes the fingerprint too");
}

// ---------------------------------------------------------------------------
// The one real provider response, committed so this runs on every build
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

void testRealLandfireResponse(const std::string& heightPath,
                              const std::string& coverPath) {
    const RasterTiff height = RasterTiff::decode(readFile(heightPath));
    const RasterTiff cover  = RasterTiff::decode(readFile(coverPath));

    check(height.width == 64 && height.height == 64,
          "the committed LANDFIRE response decodes at the size it was asked for");
    check(height.bitsPerSample == 16 &&
          height.sampleFormat == RasterTiff::Sample::SignedInt,
          "LANDFIRE canopy height arrives as signed 16-bit samples");
    check(height.georeferenced &&
          near(height.westLonDeg, -78.95, 1e-9) &&
          near(height.northLatDeg, 35.10, 1e-9) &&
          near(height.lonStepDeg, 0.1 / 64.0, 1e-9),
          "the response is placed exactly where it was requested");

    // The value histogram of this scene, over Fayetteville, North Carolina.
    // These are LANDFIRE class codes in decimetres: 230 is the 21-25 m class.
    std::map<int, int> histogram;
    for (int y = 0; y < height.height; ++y)
        for (int x = 0; x < height.width; ++x)
            ++histogram[static_cast<int>(std::lround(height.at(x, y)))];

    check(histogram[0] == 2662, "the unforested cells are counted exactly");
    check(histogram[190] == 393 && histogram[230] == 215 && histogram[150] == 274,
          "the canopy height classes decode to their expected counts");
    check(histogram[390] == 418,
          "the developed-vegetation constant is present, and is a tenth of the "
          "scene: this is the reason the substitution policy exists");
    check(histogram.count(510) == 0 && histogram.rbegin()->first == 390,
          "nothing in this scene exceeds the developed constant");

    int forestedHeight = 0, forestedCover = 0;
    for (int y = 0; y < height.height; ++y) {
        for (int x = 0; x < height.width; ++x) {
            if (height.at(x, y) > 0) ++forestedHeight;
            if (cover.at(x, y)  > 0) ++forestedCover;
        }
    }
    check(forestedHeight == 1434 && forestedCover == 1434,
          "canopy height and canopy cover agree cell for cell on where the "
          "forest is");
}

// ---------------------------------------------------------------------------
// Clearings, and the footprint the solver actually paints
// ---------------------------------------------------------------------------

VegetationArea clearingArea(float minX, float minZ, float maxX, float maxZ,
                            float heightM = 0.0f) {
    return {minX, minZ, maxX, maxZ, heightM, "cut", VegetationKind::Clearing};
}

void testClearings() {
    TerrainModel terrain = TerrainLoader::makeFlat(8, 8, 10.0f, 0.0f);
    const int width = terrain.width;
    const auto at = [width](int x, int z) {
        return static_cast<std::size_t>(z) * width + x;
    };
    const std::size_t cells = terrain.heights.size();
    const std::vector<std::uint16_t> base(cells, 180);
    const std::vector<std::uint8_t> cover(cells, 40);

    VegetationAreas::apply(terrain, {clearingArea(0.0f, 0.0f, 20.0f, 20.0f)}, base, cover);
    check(terrain.hasVegetation() && terrain.canopyHeightDm[at(1, 1)] == 0,
          "a clearing removes imported canopy inside it");
    check(terrain.hasVegetation() && terrain.canopyHeightDm[at(7, 7)] == 180 &&
          terrain.canopyCoverPct[at(7, 7)] == 40,
          "imported height and cover outside the clearing are untouched");
    check(terrain.hasVegetation() && terrain.canopyCoverPct[at(1, 1)] == 40,
          "a clearing leaves cover as it was, since with no canopy it changes no answer");

    const VegetationArea wood{0.0f, 0.0f, 40.0f, 40.0f, 12.0f, "wood"};
    const VegetationArea cut = clearingArea(10.0f, 10.0f, 30.0f, 30.0f);
    VegetationAreas::apply(terrain, {wood, cut}, base, cover);
    const std::vector<std::uint16_t> standFirstHeights = terrain.canopyHeightDm;
    const std::vector<std::uint8_t> standFirstCovers = terrain.canopyCoverPct;
    VegetationAreas::apply(terrain, {cut, wood}, base, cover);
    check(terrain.canopyHeightDm == standFirstHeights &&
          terrain.canopyCoverPct == standFirstCovers,
          "a stand and a clearing give byte-identical grids in either list order");
    check(terrain.canopyHeightDm[at(2, 2)] == 0,
          "where a clearing overlaps a drawn stand, the clearing wins");
    check(terrain.canopyHeightDm[at(0, 0)] == 120 && terrain.canopyHeightDm[at(4, 4)] == 120,
          "the stand still stands where the clearing does not reach");

    VegetationAreas::apply(terrain, {{0.0f, 0.0f, 30.0f, 30.0f, 8.0f, "low"},
                                     {20.0f, 20.0f, 60.0f, 60.0f, 20.0f, "high"}});
    check(terrain.hasVegetation() && terrain.canopyHeightDm[at(2, 2)] == 200 &&
          terrain.canopyHeightDm[at(0, 0)] == 80,
          "stands that overlap only each other still take the tallest");

    VegetationAreas::apply(terrain, {clearingArea(0.0f, 0.0f, 30.0f, 30.0f)});
    check(!terrain.hasVegetation() && terrain.canopyCoverPct.empty(),
          "clearings on terrain with no canopy leave no vegetation grids at all");

    VegetationAreas::apply(terrain, {clearingArea(-50.0f, -50.0f, 500.0f, 500.0f)},
                           base, cover);
    check(!terrain.hasVegetation() && terrain.canopyCoverPct.empty(),
          "a clearing over all the imported canopy leaves no vegetation");

    VegetationAreas::apply(terrain, {clearingArea(0.0f, 0.0f, 20.0f, 20.0f, 15.0f)},
                           base, cover);
    check(terrain.hasVegetation() && terrain.canopyHeightDm[at(1, 1)] == 0,
          "a clearing clears whatever height it happens to carry");

    const float nan = std::nanf("");
    check(clearingArea(0.0f, 0.0f, 10.0f, 10.0f).valid(),
          "a clearing with no height is valid");
    check(!VegetationArea{0.0f, 0.0f, 10.0f, 10.0f, 0.0f, "flat"}.valid(),
          "a stand with no height is not");
    check(!VegetationArea{0.0f, 0.0f, 10.0f, 10.0f, nan, "unknown"}.valid(),
          "a stand whose height is not a number is not valid");
    check(!VegetationArea{nan, 0.0f, 10.0f, 10.0f, 5.0f, "nan bound"}.valid(),
          "a bound that is not a number is not valid");
    check(!VegetationArea{0.0f, 0.0f, HUGE_VALF, 10.0f, 5.0f, "infinite"}.valid(),
          "an infinite bound is not valid");
    check(!VegetationArea{0.0f, 0.0f, 1.0e30f, 10.0f, 5.0f, "far"}.valid(),
          "a bound beyond ten thousand kilometres is not valid");
    check(!clearingArea(10.0f, 0.0f, 0.0f, 10.0f).valid(),
          "an inverted clearing is not valid");
}

void testClearingSignature() {
    TerrainModel terrain = TerrainLoader::makeFlat(8, 8, 10.0f, 0.0f);
    const std::size_t cells = terrain.heights.size();
    // Imported canopy over the northern half only, with zero cover where it is
    // bare - which is what a real LANDFIRE import carries.
    std::vector<std::uint16_t> base(cells, 0);
    std::vector<std::uint8_t> cover(cells, 0);
    for (int z = 4; z < 8; ++z) {
        for (int x = 0; x < 8; ++x) {
            base[static_cast<std::size_t>(z) * 8 + x] = 180;
            cover[static_cast<std::size_t>(z) * 8 + x] = 40;
        }
    }
    const std::vector<Antenna> antennas;
    const ReceiverSettings receiver;
    const CoverageCalibrationSettings calibration;
    auto sign = [&] {
        return SolveSignature::of(antennas, receiver, SimEngine::Mode::Planner,
                                  calibration, terrain);
    };

    VegetationAreas::apply(terrain, {}, base, cover);
    const std::string importedOnly = sign();
    VegetationAreas::apply(terrain, {clearingArea(0.0f, 0.0f, 10.0f, 10.0f)}, base, cover);
    check(sign() == importedOnly,
          "a clearing over ground that was already bare leaves the fingerprint unchanged");
    VegetationAreas::apply(terrain, {clearingArea(0.0f, 40.0f, 10.0f, 50.0f)}, base, cover);
    check(sign() != importedOnly,
          "a clearing that removes imported canopy changes the fingerprint");
}

void testFootprintMatchesRaster() {
    TerrainModel terrain = TerrainLoader::makeFlat(64, 64, 10.0f, 0.0f);
    const int width = terrain.width;
    struct Case { float lo, hi; const char* what; };
    const Case cases[] = {
        {101.0f, 199.0f, "[101, 199]"},
        {100.0f, 200.0f, "[100, 200]"},
        {101.0f, 102.0f, "[101, 102]"},
        {-500.0f, 150.0f, "[-500, 150]"},
        {5000.0f, 6000.0f, "[5000, 6000]"},
        {-9.0e6f, 9.0e6f, "[-9e6, 9e6]"},
    };
    for (const Case& c : cases) {
        const VegetationArea stand{c.lo, c.lo, c.hi, c.hi, 12.0f, "stand"};
        const VegetationAreas::Footprint fp = VegetationAreas::footprint(terrain, stand);
        VegetationAreas::apply(terrain, {stand});
        bool matches = true;
        for (int z = 0; z < terrain.height; ++z) {
            for (int x = 0; x < width; ++x) {
                const bool inFootprint = !fp.empty && x >= fp.x0 && x <= fp.x1 &&
                                         z >= fp.z0 && z <= fp.z1;
                const bool painted = terrain.hasVegetation() &&
                    terrain.canopyHeightDm[static_cast<std::size_t>(z) * width + x] > 0;
                if (inFootprint != painted) matches = false;
            }
        }
        check(matches, std::string("the painted samples are exactly the footprint for ") + c.what);
    }

    const VegetationAreas::Footprint inner =
        VegetationAreas::footprint(terrain, {101.0f, 101.0f, 199.0f, 199.0f, 12.0f, ""});
    check(inner.x0 == 10 && inner.x1 == 20 && inner.samples == 121,
          "a rectangle rounds outward, so [101, 199] paints samples 10 through 20");
    check(VegetationAreas::footprint(terrain, {101.0f, 101.0f, 102.0f, 102.0f, 12.0f, ""}).x1 == 11,
          "a rectangle narrower than a cell still paints the samples either side of it");
    check(near(inner.minX, 95.0, 1e-3) && near(inner.maxX, 205.0, 1e-3),
          "the footprint's ground reaches half a sample beyond its outermost samples");
    const VegetationAreas::Footprint edge =
        VegetationAreas::footprint(terrain, {-500.0f, -500.0f, 150.0f, 150.0f, 12.0f, ""});
    check(edge.x0 == 0 && near(edge.minX, 0.0, 1e-6),
          "a footprint's ground is clipped to the terrain");

    // Bounds typed as decimal multiples of a cell size float cannot store: their
    // float quotients are whole numbers and their double quotients are not.
    // Scenarios saved before footprint() existed were painted from the float
    // quotients, so these must not move by a sample.
    const TerrainModel oddCells = TerrainLoader::makeFlat(64, 64, 30.87f, 0.0f);
    check(VegetationAreas::footprint(oddCells, {0.0f, 0.0f, 308.7f, 308.7f, 12.0f, ""}).x1 == 10,
          "a stand ending at 308.7 m on 30.87 m cells stops at sample 10, as it always did");
    check(VegetationAreas::footprint(oddCells, {339.57f, 339.57f, 400.0f, 400.0f, 12.0f, ""}).x0 == 11,
          "a stand starting at 339.57 m on 30.87 m cells starts at sample 11, as it always did");

    const VegetationAreas::Footprint whole =
        VegetationAreas::footprint(terrain, {-9.0e6f, -9.0e6f, 9.0e6f, 9.0e6f, 12.0f, ""});
    check(!whole.empty && whole.x0 == 0 && whole.x1 == width - 1 && whole.z0 == 0 &&
              whole.z1 == terrain.height - 1,
          "a rectangle far larger than the terrain paints every sample");
    const VegetationAreas::Footprint aligned =
        VegetationAreas::footprint(terrain, {100.0f, 100.0f, 200.0f, 200.0f, 12.0f, ""});
    check(aligned.x0 == 10 && aligned.x1 == 20 && aligned.z0 == 10 && aligned.z1 == 20,
          "edges lying on sample positions paint exactly those samples");
    check(VegetationAreas::footprint(terrain, {5000.0f, 5000.0f, 6000.0f, 6000.0f, 12.0f, ""}).empty,
          "an area off the terrain has an empty footprint");
    check(VegetationAreas::countOutside(terrain, {
              {100.0f, 100.0f, 200.0f, 200.0f, 12.0f, "on"},
              {5000.0f, 5000.0f, 6000.0f, 6000.0f, 12.0f, "off"},
              {200.0f, 100.0f, 100.0f, 200.0f, 12.0f, "inverted"}}) == 1,
          "only valid areas that paint nothing are counted as outside the terrain");
}

void testSameFootprintAndImportedMedian() {
    const VegetationArea a{100.0f, 100.0f, 200.0f, 200.0f, 15.0f, "north wood"};
    VegetationArea renamed = a;
    renamed.name = "south wood";
    check(VegetationAreas::sameFootprint({a}, {renamed}),
          "a rename leaves the footprint unchanged");
    VegetationArea nearlySame = a;
    nearlySame.canopyHeightM = 15.04f;
    check(VegetationAreas::sameFootprint({a}, {nearlySame}),
          "a height change below the grid's decimetre is not a change");
    VegetationArea taller = a;
    taller.canopyHeightM = 15.1f;
    check(!VegetationAreas::sameFootprint({a}, {taller}),
          "a decimetre of height is a change");
    VegetationArea moved = a;
    moved.maxX = 210.0f;
    check(!VegetationAreas::sameFootprint({a}, {moved}), "a moved bound is a change");
    VegetationArea felled = a;
    felled.kind = VegetationKind::Clearing;
    check(!VegetationAreas::sameFootprint({a}, {felled}),
          "turning a stand into a clearing is a change");
    VegetationArea felledTaller = felled;
    felledTaller.canopyHeightM = 30.0f;
    check(VegetationAreas::sameFootprint({felled}, {felledTaller}),
          "a clearing's ignored height is not a change");
    check(!VegetationAreas::sameFootprint({a}, {a, renamed}),
          "adding an area is a change");

    TerrainModel terrain = TerrainLoader::makeFlat(64, 64, 10.0f, 0.0f);
    std::vector<std::uint16_t> base(terrain.heights.size(), 0);
    for (int z = 0; z < 64; ++z)
        for (int x = 0; x < 32; ++x)
            base[static_cast<std::size_t>(z) * 64 + x] = 180;
    const VegetationAreas::ImportedUnder whole = VegetationAreas::importedHeightUnder(
        terrain, {50.0f, 50.0f, 150.0f, 150.0f, 1.0f, ""}, base);
    check(whole.samples > 0 && whole.vegetated == whole.samples &&
          near(whole.medianM, 18.0, 1e-4),
          "a rectangle wholly over 18 m imported canopy reports that height for every sample");
    const VegetationAreas::ImportedUnder half = VegetationAreas::importedHeightUnder(
        terrain, {200.0f, 50.0f, 420.0f, 150.0f, 1.0f, ""}, base);
    const double fraction = half.samples > 0
        ? static_cast<double>(half.vegetated) / static_cast<double>(half.samples) : 0.0;
    check(fraction > 0.4 && fraction < 0.6,
          "a rectangle half over imported canopy reports about half its samples vegetated");
    check(VegetationAreas::importedHeightUnder(
              terrain, {50.0f, 50.0f, 150.0f, 150.0f, 1.0f, ""}, {}).samples == 0,
          "with no import there is nothing under an area");
}

} // namespace

int main(int argc, char** argv) {
    try {
        testTiffLayouts();
        testTiffSampleFormats();
        testTiffRejections();
        testTiffGeoreferencing();
        testRegionAndUrl();
        testBoundsCoverSampleCentres();
        testAssemble();
        testAssembleRejections();
        testSampleOnto();
        testComposite();
        testCoverScalesDepth();
        testSignatureSeesCover();
        testClearings();
        testClearingSignature();
        testFootprintMatchesRaster();
        testSameFootprintAndImportedMedian();

        if (argc >= 3) {
            testRealLandfireResponse(argv[1], argv[2]);
        } else {
            std::cout << "[FAIL] the committed LANDFIRE fixtures were not supplied\n";
            ++failures;
        }
    } catch (const std::exception& e) {
        std::cout << "[FAIL] unexpected exception: " << e.what() << "\n";
        ++failures;
    }

    std::cout << (failures == 0 ? "All canopy tests passed\n"
                                : std::to_string(failures) + " canopy test(s) failed\n");
    return failures == 0 ? 0 : 1;
}
