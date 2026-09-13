#include "LandfireCanopy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <stdexcept>

namespace landfire {
namespace {

constexpr const char* kServiceRoot =
    "https://lfps.usgs.gov/arcgis/rest/services";

// LANDFIRE writes -9999 for no data. Anything strongly negative is treated the
// same way rather than only the exact sentinel, because a resampled edge pixel
// can come back as a neighbouring negative.
constexpr double kNoDataCeiling = -1000.0;

// The tallest canopy the decimetre field can carry. LANDFIRE's own top class is
// open-ended above 500 dm, so a clamp is needed somewhere; putting it here
// keeps TerrainModel's grid honest about what it holds.
constexpr int kMaxCanopyDm = 65000;

// Half a pixel of slack: the service returns doubles that differ from the
// requested box in the last bits, and rejecting on that would reject everything.
constexpr double kPlacementTolerance = 1e-6;

std::string trimTrailingZeros(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.8f", value);
    std::string text(buffer);
    const std::size_t lastDigit = text.find_last_not_of('0');
    if (lastDigit != std::string::npos && text[lastDigit] == '.')
        text.erase(lastDigit + 2);
    else if (lastDigit != std::string::npos)
        text.erase(lastDigit + 1);
    return text;
}

void replaceAll(std::string& text, const std::string& token,
                const std::string& value) {
    std::size_t at = 0;
    while ((at = text.find(token, at)) != std::string::npos) {
        text.replace(at, token.size(), value);
        at += value.size();
    }
}

void requirePlacement(const RasterTiff& raster, const Bounds& requested,
                      const char* layerName) {
    if (!raster.georeferenced) return;   // nothing to check against
    const double lonStep = (requested.east - requested.west) / raster.width;
    const double latStep = (requested.north - requested.south) / raster.height;
    const bool placed =
        std::abs(raster.westLonDeg  - requested.west)  <= kPlacementTolerance &&
        std::abs(raster.northLatDeg - requested.north) <= kPlacementTolerance &&
        std::abs(raster.lonStepDeg  - lonStep) <= kPlacementTolerance &&
        std::abs(raster.latStepDeg  - latStep) <= kPlacementTolerance;
    if (!placed)
        throw std::runtime_error(
            std::string("The ") + layerName + " raster came back covering a "
            "different area than was requested, so it cannot be trusted to line "
            "up with the terrain.");
}

} // namespace

Region regionFor(double latDeg, double lonDeg) {
    if (!std::isfinite(latDeg) || !std::isfinite(lonDeg)) return Region::None;
    // Generous boxes around each mosaic. A point inside the box but outside the
    // data still succeeds here and is caught by the no-data count after the
    // fetch, which is the check that actually knows.
    if (latDeg >= 24.0 && latDeg <= 49.5 && lonDeg >= -125.5 && lonDeg <= -66.5)
        return Region::Conus;
    if (latDeg >= 51.0 && latDeg <= 72.0 &&
        (lonDeg <= -129.0 || lonDeg >= 172.0))
        return Region::Alaska;
    if (latDeg >= 18.5 && latDeg <= 22.5 && lonDeg >= -160.5 && lonDeg <= -154.5)
        return Region::Hawaii;
    if (latDeg >= 17.5 && latDeg <= 18.6 && lonDeg >= -68.0 && lonDeg <= -64.5)
        return Region::PuertoRicoVI;
    return Region::None;
}

const char* regionSuffix(Region region) {
    switch (region) {
        case Region::Conus:        return "CONUS";
        case Region::Alaska:       return "AK";
        case Region::Hawaii:       return "HI";
        case Region::PuertoRicoVI: return "PRVI";
        case Region::None:         break;
    }
    return "";
}

const char* regionDisplayName(Region region) {
    switch (region) {
        case Region::Conus:        return "the contiguous United States";
        case Region::Alaska:       return "Alaska";
        case Region::Hawaii:       return "Hawaii";
        case Region::PuertoRicoVI: return "Puerto Rico and the US Virgin Islands";
        case Region::None:         break;
    }
    return "outside LANDFIRE coverage";
}

std::string defaultProduct() {
    if (const char* configured = std::getenv("RIDGELINE_CANOPY_PRODUCT"))
        if (*configured) return configured;
    return "LF2024";
}

const char* layerCode(Layer layer) {
    switch (layer) {
        case Layer::CanopyHeight:    return "CH";
        case Layer::CanopyCover:     return "CC";
        case Layer::VegetationHeight: return "EVH";
    }
    return "CH";
}

Bounds boundsForTerrain(const TerrainModel& terrain) {
    Bounds bounds;
    if (!terrain.hasGeoReference() || terrain.width < 2 || terrain.height < 2)
        return bounds;
    const double halfLon = terrain.cellLonDeg * 0.5;
    const double halfLat = terrain.cellLatDeg * 0.5;
    bounds.west  = terrain.originLonDeg - halfLon;
    bounds.east  = terrain.originLonDeg + (terrain.width - 1) * terrain.cellLonDeg
                 + halfLon;
    bounds.south = terrain.originLatDeg - halfLat;
    bounds.north = terrain.originLatDeg + (terrain.height - 1) * terrain.cellLatDeg
                 + halfLat;
    return bounds;
}

std::string exportImageUrl(const std::string& product, Region region, Layer layer,
                           const Bounds& bounds, int width, int height,
                           const std::string& overrideTemplate) {
    const std::string bbox =
        trimTrailingZeros(bounds.west)  + "," + trimTrailingZeros(bounds.south) +
        "," + trimTrailingZeros(bounds.east) + "," + trimTrailingZeros(bounds.north);

    std::string url = overrideTemplate;
    if (url.empty()) {
        url = std::string(kServiceRoot) +
              "/Landfire_{product}/{product}_{layer}_{region}/ImageServer/exportImage"
              "?bbox={bbox}&bboxSR=4326&imageSR=4326&size={width},{height}"
              "&format=tiff&interpolation=RSP_NearestNeighbor&f=image";
    }
    replaceAll(url, "{product}", product);
    replaceAll(url, "{layer}",   layerCode(layer));
    replaceAll(url, "{region}",  regionSuffix(region));
    replaceAll(url, "{bbox}",    bbox);
    replaceAll(url, "{width}",   std::to_string(width));
    replaceAll(url, "{height}",  std::to_string(height));
    return url;
}

bool isDevelopedVegetation(int evhValue) {
    return evhValue >= 13 && evhValue <= 17;
}

std::string nowUtcIso8601() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

CanopyRaster assemble(const RasterTiff& canopyHeight,
                      const RasterTiff& canopyCover,
                      const RasterTiff& vegetationHeight,
                      const Bounds& requested,
                      const Options& options,
                      const std::string& product,
                      const std::string& retrievedUtc) {
    if (!requested.valid())
        throw std::runtime_error("The requested canopy area has no extent.");
    if (canopyHeight.width <= 0 || canopyHeight.height <= 0)
        throw std::runtime_error("The canopy height raster is empty.");
    if (canopyCover.width != canopyHeight.width ||
        canopyCover.height != canopyHeight.height ||
        vegetationHeight.width != canopyHeight.width ||
        vegetationHeight.height != canopyHeight.height)
        throw std::runtime_error(
            "The canopy height, cover and vegetation rasters are different "
            "sizes, so they cannot describe the same ground.");

    requirePlacement(canopyHeight,    requested, "canopy height");
    requirePlacement(canopyCover,     requested, "canopy cover");
    requirePlacement(vegetationHeight, requested, "vegetation height");

    CanopyRaster out;
    out.width  = canopyHeight.width;
    out.height = canopyHeight.height;
    out.westLonDeg  = requested.west;
    out.northLatDeg = requested.north;
    out.lonStepDeg  = (requested.east - requested.west) / out.width;
    out.latStepDeg  = (requested.north - requested.south) / out.height;

    const std::size_t cells = static_cast<std::size_t>(out.width) * out.height;
    out.heightDm.assign(cells, 0);
    out.coverPct.assign(cells, 0);

    const int assumedDm = std::clamp(
        static_cast<int>(std::lround(options.assumedDevelopedCanopyM * 10.0f)),
        0, kMaxCanopyDm);

    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * out.width + x;
            const double rawHeight = canopyHeight.at(x, y);
            const double rawCover  = canopyCover.at(x, y);
            const double rawVeg    = vegetationHeight.at(x, y);

            ++out.coverage.samples;
            // No-data in the height layer is the one that decides: without a
            // height there is nothing to attenuate, whatever the others say.
            if (rawHeight <= kNoDataCeiling) continue;
            ++out.coverage.withData;

            int heightDm = static_cast<int>(std::lround(rawHeight));
            if (heightDm <= 0) continue;

            int coverPct = rawCover > kNoDataCeiling
                         ? static_cast<int>(std::lround(rawCover)) : 100;

            const bool assigned =
                rawVeg > kNoDataCeiling &&
                isDevelopedVegetation(static_cast<int>(std::lround(rawVeg)));
            if (assigned) {
                switch (options.developed) {
                    case DevelopedPolicy::TreatAsBare:   heightDm = 0; break;
                    case DevelopedPolicy::AssumeHeight:  heightDm = assumedDm; break;
                    case DevelopedPolicy::UseSourceValue: break;
                }
                if (heightDm > 0) ++out.coverage.assignedHeight;
            }
            if (heightDm <= 0) continue;

            out.heightDm[index] =
                static_cast<std::uint16_t>(std::clamp(heightDm, 0, kMaxCanopyDm));
            out.coverPct[index] =
                static_cast<std::uint8_t>(std::clamp(coverPct, 0, 100));
            ++out.coverage.vegetated;
        }
    }

    out.provenance.sourceId       = "landfire-" + product;
    out.provenance.displayName    = "LANDFIRE canopy height and cover";
    out.provenance.productVintage = "LANDFIRE " + product;
    out.provenance.licence        = "Public domain (US Government work)";
    out.provenance.attribution =
        "LANDFIRE, US Department of the Interior and US Department of Agriculture";
    out.provenance.retrievedUtc       = retrievedUtc;
    out.provenance.sourceResolutionM  = 30.0;
    return out;
}

} // namespace landfire
