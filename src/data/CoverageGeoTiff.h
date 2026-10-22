#pragma once
#include <cstdint>
#include <vector>

#include "sim/TerrainModel.h"
#include "sim/Types.h"

// A georeferenced analysis raster, as distinct from a screen capture: one
// float32 band of a result metric, one pixel per result cell, placed in WGS 84
// longitude/latitude so a GIS can overlay it on anything else.
//
// Each pixel is the area its result cell stands for (PixelIsArea), with the
// solved value taken at the cell centre. The last row and column can reach up
// to one partial cell beyond the terrain edge, where the solver clamps its
// sample point to the boundary. Cells with no value for the metric are NaN,
// declared as no-data; the service band is 1 in service and 0 out, everywhere.
//
// Classic uncompressed little-endian TIFF, the same shape RasterTiff reads, so
// the export is verifiable without GDAL. std-only.
namespace CoverageGeoTiff {

// Which result metric the band carries. One per file keeps the export in the
// single-band shape RasterTiff verifies.
enum class Metric { DesiredSignal, Sinr, FadeMargin, Service };

// The band's description and unit as written into the file, e.g.
// "Desired signal" and "dBm". The service band has no unit.
const char* description(Metric metric);
const char* unit(Metric metric);

// False when the grid is invalid or the terrain has no real-world location;
// a raster with no placement is not a GIS export.
bool canEncode(const FieldGrid& grid, const TerrainModel& terrain);

// Empty when canEncode is false.
std::vector<std::uint8_t> encode(const FieldGrid& grid, const TerrainModel& terrain,
                                 Metric metric = Metric::DesiredSignal);

} // namespace CoverageGeoTiff
