#pragma once

#include <string>

struct MgrsCoordinate {
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    int precisionDigits = 0;
};

// Converts a UTM-based MGRS reference (bands C-X) to WGS84. References with
// fewer than five easting/northing digits resolve to the centre of their grid
// square, which is the least surprising interpretation for an analysis AOI.
bool mgrsToWgs84(const std::string& text,
                 MgrsCoordinate& coordinate,
                 std::string& error);

// Converts WGS84 to a UTM-based MGRS reference at 0-5 digit precision per
// axis. Polar UPS bands are intentionally rejected because Ridgeline's streamed
// terrain workflow is limited to the Web Mercator/UTM operating latitudes.
bool wgs84ToMgrs(double latitudeDeg,
                 double longitudeDeg,
                 int precisionDigits,
                 std::string& mgrs,
                 std::string& error);
