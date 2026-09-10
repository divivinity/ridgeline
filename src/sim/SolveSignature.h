#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "SimEngine.h"

#include <string>
#include <vector>

// A fingerprint of everything a coverage solve depends on.
//
// A displayed coverage field is only meaningful for the inputs it was computed
// from. Change a frequency, move a source, switch propagation mode or load new
// terrain and the picture on screen is describing a scenario that no longer
// exists — but it keeps looking authoritative. Comparing this fingerprint
// against the one captured at solve time is what makes that detectable.
//
// Everything that changes the result belongs here; nothing that does not.
// Display-only state such as layer visibility or camera position is excluded on
// purpose, so merely looking at the map never marks a result stale.
namespace SolveSignature {

std::string of(const std::vector<Antenna>& antennas,
               const ReceiverSettings& receiver,
               SimEngine::Mode mode,
               const CoverageCalibrationSettings& calibration,
               const TerrainModel& terrain);

} // namespace SolveSignature
