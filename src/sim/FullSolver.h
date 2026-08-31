#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "MaterialLibrary.h"
#include <functional>
#include <vector>

class FullSolver {
public:
    float gridResolution = 10.0f;  // metres per grid cell
    float rainRateMmH    = 4.0f;   // moderate rain (mm/h); 0 = disable
    bool  enableGroundReflection = true;
    bool  enableRain = true;
    bool  enableFresnelPenalty = true;
    float requiredFresnelClearance = 0.6f;  // 60% first-zone clearance field rule

    FieldGrid solve(const TerrainModel& terrain,
                    const std::vector<Antenna>& antennas,
                    const MaterialLibrary& materials,
                    const std::function<void(int, int)>& progress = {}) const;

private:
    // Friis FSPL in dB
    static float fsplDb(float distM, float freqMHz);

    // Fresnel power reflection coefficient [0,1], vertical polarisation
    static float fresnelPowerRefl(float eps_r, float sigma_sm,
                                   float freqMHz, float grazingAngleRad);

    // Knife-edge diffraction loss in dB (ITU-R P.526-15 piecewise)
    static float knifeEdgeLossDb(float nu);

    // Rain attenuation in dB (ITU-R P.838-3 vertical polarisation)
    static float rainAttenuationDb(float rainRateMmH, float freqMHz, float distKm);
};
