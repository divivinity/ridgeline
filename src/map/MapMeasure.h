#pragma once
#include <QString>

// Measurement and map-furniture maths. Deliberately free of Qt Widgets and
// OpenGL so it can be tested without a display.
namespace MapMeasure {

// Great-circle distance on a spherical earth. Ridgeline's terrain grids span
// tens of kilometres, where the spherical error against an ellipsoid is well
// under the DEM's own sample spacing, so the extra complexity of Vincenty
// would not buy accuracy that survives the terrain data.
double haversineMetres(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg);

// Initial great-circle bearing, degrees clockwise from true north in [0, 360).
double initialBearingDeg(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg);

// Distance in the terrain's local metric plane, for scenes with no
// georeference where latitude and longitude do not exist.
double planarMetres(double x1, double z1, double x2, double z2);

// A scale bar rounded to a readable interval.
struct ScaleBar {
    double metres = 0.0;   // the distance the bar represents
    double pixels = 0.0;   // how wide to draw it
    QString label;         // formatted with a unit, e.g. "500 m" or "2 km"
    bool valid() const { return metres > 0.0 && pixels > 0.0; }
};

// Picks the largest 1/2/5 x 10^n distance whose bar fits maxPixels. Returns an
// invalid bar when the scale is not finite or positive, which happens before
// the first frame has established a camera.
ScaleBar chooseScaleBar(double metresPerPixel, double maxPixels);

// Formats a distance with a unit that suits its magnitude.
QString formatDistance(double metres);

// Compass point for a bearing, e.g. 0 -> "N", 45 -> "NE".
QString compassPoint(double bearingDeg);

} // namespace MapMeasure
