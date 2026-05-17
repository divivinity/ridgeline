#pragma once
#include "sim/TerrainModel.h"
#include <string>

class TerrainLoader {
public:
    static TerrainModel makeFlat(int widthCells, int heightCells,
                                 float cellSize, float elevation = 0.0f);

    static TerrainModel makeHills(int widthCells, int heightCells,
                                  float cellSize, float amplitude = 40.0f);

    // Parse a DTED Level 0/1/2 file (.dt0/.dt1/.dt2).
    // Throws std::runtime_error on parse failure.
    static TerrainModel fromDTED(const std::string& path);
};
