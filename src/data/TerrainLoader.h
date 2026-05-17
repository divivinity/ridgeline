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

    // Parse an SRTM HGT file (.hgt).  File size determines resolution:
    //   3601×3601×2 bytes → SRTM1 (~30 m/cell)
    //   1201×1201×2 bytes → SRTM3 (~90 m/cell)
    // cropCenterLat/Lon select the 512×512 sub-region to load.
    // Defaults aim at Fayetteville NC (35.05 °N, 78.88 °W).
    // Throws std::runtime_error on parse failure.
    static TerrainModel fromHGT(const std::string& path,
                                float cropCenterLat = 35.05f,
                                float cropCenterLon = -78.88f,
                                int   cropSize      = 512);
};
