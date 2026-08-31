#pragma once

#include "sim/TerrainModel.h"

// Downloads a location-centred elevation mosaic from the public Mapzen
// Terrain Tiles dataset in the AWS Open Data Registry. Terrarium RGB values
// are decoded to metres and resampled onto a regular WGS84 grid so the RF
// solver, basemap, exports, and cursor coordinates share one reference frame.
class GlobalTerrainLoader {
public:
    static TerrainModel load(double centerLat,
                             double centerLon,
                             double extentKm = 15.0,
                             int samplesPerAxis = 513);
};
