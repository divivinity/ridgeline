#pragma once

#include "TerrainModel.h"
#include "MaterialLibrary.h"
#include "Types.h"
#include <glm/glm.hpp>

enum class ItmOutcome { Valid, NotBuilt, ShortPath, OutOfBand, Rejected };

struct ItmPrediction {
    bool valid = false;
    ItmOutcome outcome = ItmOutcome::NotBuilt;
    float basicTransmissionLossDb = 0.0f;
    long warnings = 0;
    int errorCode = 0;
};

class ItmPropagation {
public:
    static ItmPrediction predict(const TerrainModel& terrain,
                                 glm::vec3 tx,
                                 glm::vec3 rx,
                                 const Antenna& antenna,
                                 const Material& material);
};
