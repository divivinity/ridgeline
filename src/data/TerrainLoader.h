#pragma once
#include "sim/TerrainModel.h"
#include <string>
#include <vector>

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

    // Load a location-centred, georeferenced mosaic from a directory of SRTM
    // HGT tiles. Adjacent tiles are selected automatically, so locations near
    // a one-degree tile edge remain centred on the requested coordinates.
    // extentKm is the approximate east-west and north-south coverage.
    static TerrainModel fromHGTDirectory(const std::string& directory,
                                         double centerLat,
                                         double centerLon,
                                         double extentKm = 15.0,
                                         int maxSamplesPerAxis = 1024);

    // Return the tile filenames needed for a location-centred HGT mosaic.
    // Useful for validating a terrain library before starting a load.
    static std::vector<std::string> requiredHGTTiles(double centerLat,
                                                     double centerLon,
                                                     double extentKm);

    // Convert the southwest integer-degree corner to an SRTM filename.
    static std::string hgtTileName(int southwestLat, int southwestLon);

    // Parse a GDAL-supported DEM raster, primarily USGS 3DEP GeoTIFF.
    // Large rasters are resampled to maxSamples per side for interactive use.
    static TerrainModel fromGeoTIFF(const std::string& path,
                                    int maxSamples = 1024);
};
