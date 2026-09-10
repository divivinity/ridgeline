#pragma once
#include <QImage>

class TerrainModel;

namespace Hillshade {

// Lambertian relief shading computed from true elevations and the model's real
// sample spacing in metres.
//
// Never feed this the viewport's vertically exaggerated display mesh: the
// viewport scales Y for legibility, and shading the stretched geometry would
// overstate every slope. Spacing differs between the east-west and north-south
// axes at most latitudes, so both are used rather than one nominal cell size.
//
// Returns a grayscale image with one pixel per terrain sample, oriented top-down
// for compositing (the terrain grid stores rows south to north). Returns a null
// image for a grid smaller than 2x2 or with non-positive sample spacing.
QImage render(const TerrainModel& terrain, double azimuthDeg, double altitudeDeg);

} // namespace Hillshade
