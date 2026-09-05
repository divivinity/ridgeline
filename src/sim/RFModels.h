#pragma once

#include "Types.h"
#include <glm/glm.hpp>

namespace RFModels {

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
