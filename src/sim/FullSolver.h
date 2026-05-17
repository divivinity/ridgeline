#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "MaterialLibrary.h"
#include <vector>

class FullSolver {
public:
    float gridResolution = 10.0f;
    float rainRateMmH    = 4.0f;

    FieldGrid solve(const TerrainModel& terrain,
                    const std::vector<Antenna>& antennas,
                    const MaterialLibrary& materials) const;
};
