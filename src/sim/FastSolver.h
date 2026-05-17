#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include <vector>

class FastSolver {
public:
    // Grid resolution: one cell per this many metres
    float gridResolution = 10.0f;

    FieldGrid solve(const TerrainModel& terrain,
                    const std::vector<Antenna>& antennas) const;

private:
    // Friis free-space path loss in dB
    static float fsplDb(float distM, float freqMHz);

    // dB loss from materials (placeholder — returns 0.0 until MaterialLibrary added)
    static float materialLossDb(float distM);
};
