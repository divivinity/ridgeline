#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "MaterialLibrary.h"
#include <functional>
#include <vector>

class FastSolver {
public:
    float gridResolution = 10.0f;  // metres per grid cell

    FieldGrid solve(const TerrainModel& terrain,
                    const std::vector<Antenna>& antennas,
                    const MaterialLibrary& materials,
                    const std::function<void(int, int)>& progress = {}) const;

private:
    static float fsplDb(float distM, float freqMHz);
};
