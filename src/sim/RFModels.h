#pragma once

#include "Types.h"
#include "TerrainModel.h"
#include <glm/glm.hpp>

namespace RFModels {

// Validate once before sampling terrain or allocating a coverage grid.
void validateSolveInputs(const TerrainModel& terrain,
                         const std::vector<Antenna>& antennas,
                         const ReceiverSettings& receiver, float resolutionM);

// Excess loss through vegetation, in dB, for a path passing through `depthM`
// metres of canopy.
//
// HONESTY NOTE, load-bearing: this is an engineering extrapolation, not a
// standards-conformant calculation. ITU-R P.833-10 has no sanctioned
// terrestrial model for Ridgeline's actual geometry - extensive woodland
// mid-path with BOTH terminals outside it. Its equation (1) is explicitly
// excluded for that case ("does not apply for a radio path obstructed by a
// single vegetative obstruction where both terminals are outside the
// vegetative medium"), and equation (7)'s simple gamma*d form is scoped to
// "at or below 1 GHz".
//
// What is borrowed is the SHAPE - exponential saturation toward a ceiling -
// because that is physically right: a path around or over the canopy
// eventually wins, so loss cannot grow without bound. A linear dB/m form,
// which is what this codebase shipped, reaches ~96 dB at 1 km of woodland
// against a measured asymptote near 26.5 dB.
//
// Aev = Am(f) * [1 - exp(-d * gamma(f) / Am(f))]
//
// Accuracy is roughly 5-10 dB RMS at UHF even for models tuned to a site, and
// nothing here has been validated against field measurements.
float vegetationLossDb(float depthM, float freqMHz);

// Specific attenuation in dB/m at zero depth, and the saturation ceiling in dB.
// Exposed for testing and for explaining a result, not for callers to combine
// themselves.
float vegetationGammaDbPerM(float freqMHz);
float vegetationMaxLossDb(float freqMHz);

float dbmToMw(float dbm);
float mwToDbm(float mw);
float noiseFloorDbm(const ReceiverSettings& receiver);
float polarizationMismatchLossDb(const std::string& txPolarization,
                                 const std::string& rxPolarization);
float effectiveAntennaGainDbi(const Antenna& antenna,
                              const glm::vec3& target,
                              const ReceiverSettings& receiver);
void initializeMetrics(FieldGrid& grid);
void finalizeMetrics(FieldGrid& grid, const ReceiverSettings& receiver);

} // namespace RFModels
