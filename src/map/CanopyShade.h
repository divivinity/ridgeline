#pragma once
#include <QImage>

class TerrainModel;

namespace CanopyShade {

// The tallest canopy the colour ramp distinguishes. Above this everything reads
// as the darkest green; LANDFIRE's own top class is open-ended, and a ramp that
// stretched to fit a single redwood would flatten every ordinary wood.
constexpr float kRampCeilingM = 40.0f;

// A green wash over the vegetated part of the terrain, one pixel per sample.
//
// Height sets the hue, running from pale scrub to deep forest, and crown cover
// sets the opacity, so thin cover reads as thin on the map. Bare samples are
// fully transparent, which is what lets this be composited over imagery without
// hiding it.
//
// Returned top-down for compositing, matching Hillshade::render: the terrain
// grid stores rows south to north and the basemap flips once at the end.
//
// Returns a null image when the terrain carries no vegetation at all, which the
// caller should read as "nothing to draw" rather than as a failure.
QImage render(const TerrainModel& terrain);

} // namespace CanopyShade
