#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A baseline TIFF reader for rasters Ridgeline fetches for itself.
//
// Ridgeline already reads GeoTIFF through GDAL, but that path is optional at
// configure time, so a feature built on it cannot be built at all without the
// dependency. This decoder covers exactly the shape the canopy services return
// - uncompressed, single band, classic TIFF - and refuses everything else by
// name rather than guessing. It is deliberately small and deliberately strict:
// a reader that quietly mis-decodes a raster puts vegetation in the wrong place
// and nothing downstream can tell.
//
// std-only, so it runs in the headless test configure next to TerrainModel.
struct RasterTiff {
    enum class Sample { UnsignedInt, SignedInt, Float };

    int width  = 0;
    int height = 0;
    int bitsPerSample = 0;
    Sample sampleFormat = Sample::UnsignedInt;

    // Row-major, first row first, in the file's own orientation. Widened to
    // double so one accessor serves every supported sample format without
    // losing an exact integer value.
    std::vector<double> values;

    // Set when the file carries ModelPixelScale (33550) and ModelTiepoint
    // (33922) for a north-up, axis-aligned raster, which is what a
    // geographic-projection export produces. Steps are positive degrees per
    // pixel; latitude decreases as the row index grows.
    bool   georeferenced = false;
    double westLonDeg    = 0.0;
    double northLatDeg   = 0.0;
    double lonStepDeg    = 0.0;
    double latStepDeg    = 0.0;

    double at(int x, int y) const;

    // Throws std::runtime_error, naming what was unsupported, on anything this
    // decoder does not handle.
    static RasterTiff decode(const std::vector<std::uint8_t>& bytes);
};
