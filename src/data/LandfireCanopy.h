#pragma once
#include "CanopyRaster.h"
#include "RasterTiff.h"
#include "sim/TerrainModel.h"

#include <string>

// Canopy height and cover from LANDFIRE, the US interagency vegetation product.
//
// Chosen over the global alternatives for three reasons, each checked against
// the live service rather than assumed:
//
//   1. Its ArcGIS ImageServer will export an arbitrary bounding box at an
//      arbitrary pixel size in WGS84, so the provider does the reprojection and
//      resampling and hands back a grid that already lines up with the terrain.
//   2. It is a US Government work in the public domain, so it can ship inside
//      an offline package today. The CC BY alternatives cannot until the
//      package format can carry a licence notice.
//   3. Canopy height arrives in decimetres directly. Land-cover class products
//      cannot produce a depth at all.
//
// Its cost is coverage: the United States and its territories only.
namespace landfire {

enum class Region { None, Conus, Alaska, Hawaii, PuertoRicoVI };

// Which regional mosaic covers a point, or None where LANDFIRE does not reach.
Region regionFor(double latDeg, double lonDeg);
const char* regionSuffix(Region region);     // "CONUS", "AK", ...
const char* regionDisplayName(Region region);

// The LANDFIRE release to read. 2024 is the default because it is the newest
// with all four regional mosaics; 2025 exists for CONUS and Alaska only.
std::string defaultProduct();

struct Bounds {
    double west = 0.0, south = 0.0, east = 0.0, north = 0.0;
    bool valid() const { return east > west && north > south; }
};

// The box to request so that returned pixel *centres* land exactly on the
// terrain's sample positions.
//
// A terrain grid is defined by its sample points; an image bounding box is
// defined by its outer edges. Asking for the box that spans the samples would
// offset every pixel centre by half a cell, so the box is grown by half a cell
// on each side. Getting this wrong shifts the whole canopy layer diagonally by
// half a sample, which is invisible on screen and wrong in the solver.
Bounds boundsForTerrain(const TerrainModel& terrain);

enum class Layer {
    CanopyHeight,     // CH: forest canopy height, decimetres
    CanopyCover,      // CC: forest canopy cover, percent
    VegetationHeight  // EVH: existing vegetation height, class codes
};

const char* layerCode(Layer layer);

// Honours RIDGELINE_CANOPY_URL when set, substituting {product}, {layer},
// {region}, {bbox}, {width} and {height} by name, the way the terrain provider
// override already works.
std::string exportImageUrl(const std::string& product, Region region, Layer layer,
                           const Bounds& bounds, int width, int height,
                           const std::string& overrideTemplate = {});

// True for the EVH classes that describe vegetation inside developed land:
// Developed-Upland Deciduous Forest (13), Evergreen Forest (14), Mixed Forest
// (15), Herbaceous (16) and Shrubland (17).
bool isDevelopedVegetation(int evhValue);

// What to do where LANDFIRE's canopy height is an assigned constant rather than
// a measurement. See the note on `Options::developed`.
enum class DevelopedPolicy {
    AssumeHeight,    // substitute a stated assumption, and count it
    UseSourceValue,  // pass LANDFIRE's constant through unchanged
    TreatAsBare      // drop the canopy entirely
};

struct Options {
    // Over developed land LANDFIRE does not measure canopy height; it assigns a
    // nationwide constant per vegetation type. Sampled over five scenes, 1007
    // of 1008 Developed-Upland Evergreen Forest cells carried canopy height
    // exactly 390 (37-41 m) at exactly 45% cover, and 201 of 202 Developed
    // Deciduous cells carried exactly 190 (17-21 m) at 35%.
    //
    // Passing that through would paint a uniform 39 m evergreen canopy across
    // every American suburb - roughly a tenth of a typical residential scene,
    // and precisely where short links are planned. Dropping it instead deletes
    // urban tree cover, which is real and does attenuate. Neither silent choice
    // is defensible, so the substitution is explicit, adjustable, and counted
    // in CanopyCoverage::assignedHeight so the operator is told.
    DevelopedPolicy developed = DevelopedPolicy::AssumeHeight;

    // The stated assumption. Ten metres is a mature residential tree; it is an
    // assumption, not a measurement, and the UI must present it as one.
    float assumedDevelopedCanopyM = 10.0f;
};

// Assemble the three exported rasters into one canopy layer.
//
// Throws std::runtime_error, naming the discrepancy, when the rasters disagree
// in size or when their embedded georeferencing does not match the box that was
// requested. A canopy layer placed somewhere other than where it was asked for
// is worse than no canopy layer: nothing downstream can detect it.
CanopyRaster assemble(const RasterTiff& canopyHeight,
                      const RasterTiff& canopyCover,
                      const RasterTiff& vegetationHeight,
                      const Bounds& requested,
                      const Options& options,
                      const std::string& product,
                      const std::string& retrievedUtc);

// ISO-8601 UTC, for CanopyProvenance::retrievedUtc.
std::string nowUtcIso8601();

} // namespace landfire
