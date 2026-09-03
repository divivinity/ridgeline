#pragma once

#include "sim/TerrainModel.h"
#include <functional>

// Downloads a location-centred elevation mosaic from the public Mapzen
// Terrain Tiles dataset in the AWS Open Data Registry. Terrarium RGB values
// are decoded to metres and resampled onto a regular WGS84 grid so the RF
// solver, basemap, exports, and cursor coordinates share one reference frame.
class GlobalTerrainLoader {
public:
    using ProgressCallback = std::function<void(int completed, int total)>;
    using CancelCallback = std::function<bool()>;

    static TerrainModel load(double centerLat,
                             double centerLon,
                             double extentKm = 15.0,
                             int samplesPerAxis = 513,
                             const ProgressCallback& progress = {},
                             const CancelCallback& cancel = {});
};
