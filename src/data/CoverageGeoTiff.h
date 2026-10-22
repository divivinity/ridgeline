#pragma once
#include <cstdint>
#include <vector>

#include "sim/TerrainModel.h"
#include "sim/Types.h"

// A georeferenced analysis raster, as distinct from a screen capture: one
// float32 band of desired signal in dBm, one pixel per result cell, placed in
// WGS 84 longitude/latitude so a GIS can overlay it on anything else.
//
// Each pixel is the area its result cell stands for (PixelIsArea), with the
// solved value taken at the cell centre. The last row and column can reach up
// to one partial cell beyond the terrain edge, where the solver clamps its
// sample point to the boundary. Cells with no signal are NaN, declared as
// no-data.
//
// Classic uncompressed little-endian TIFF, the same shape RasterTiff reads, so
// the export is verifiable without GDAL. std-only.
namespace CoverageGeoTiff {

// False when the grid is invalid or the terrain has no real-world location;
// a raster with no placement is not a GIS export.
bool canEncode(const FieldGrid& grid, const TerrainModel& terrain);

// Empty when canEncode is false.
std::vector<std::uint8_t> encode(const FieldGrid& grid, const TerrainModel& terrain);

} // namespace CoverageGeoTiff
