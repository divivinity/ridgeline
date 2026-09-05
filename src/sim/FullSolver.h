#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "MaterialLibrary.h"
#include <functional>
#include <vector>

class FullSolver {
public:
    float gridResolution = 10.0f;
    float rainRateMmH    = 4.0f;
    bool  enableGroundReflection = true;
    bool  enableRain = true;
    bool  enableFresnelPenalty = true;
    bool  enableMultipleDiffraction = true;
    bool  enableItm = false;
    float requiredFresnelClearance = 0.6f;
    ReceiverSettings receiver;

    FieldGrid solve(const TerrainModel& terrain,
                    const std::vector<Antenna>& antennas,
                    const MaterialLibrary& materials,
                    const std::function<void(int, int)>& progress = {},
                    const std::function<bool()>& cancelled = {}) const;

private:
    static float fsplDb(float distM, float freqMHz);
    static float fresnelPowerRefl(float eps_r, float sigma_sm,
                                   float freqMHz, float grazingAngleRad);
    static float knifeEdgeLossDb(float nu);
    static float rainAttenuationDb(float rainRateMmH, float freqMHz, float distKm);
    static float multipleKnifeEdgeLossDb(const TerrainModel& terrain,
                                         glm::vec3 from,
                                         glm::vec3 to,
                                         float frequencyMHz);
    static float itmBasicTransmissionLossDb(const TerrainModel& terrain,
                                            glm::vec3 from,
                                            glm::vec3 to,
                                            float frequencyMHz,
                                            const Material& material,
                                            const Antenna& antenna);
};
