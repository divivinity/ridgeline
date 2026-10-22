#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <limits>
#include "sim/Types.h"
#include "sim/SimEngine.h"
#include "sim/VegetationAreas.h"

// The newest scenario format this build reads. A file declaring a higher
// minReaderVersion is refused with a message rather than half-understood.
inline constexpr int kScenarioReaderVersion = 6;

struct TerrainSource {
    enum class Type { Hills, Flat, Hgt, Dted, GeoTiff, Global };

    Type        type         = Type::Hills;
    std::string hgtPath;
    double      hgtLat       = 35.05;
    double      hgtLon       = -78.88;
    int         hgtSize      = 512;

    double      centerLat    = 35.5951;
    double      centerLon    = -82.5515;
    double      extentKm     = 15.0;

    int   widthCells  = 200;
    int   heightCells = 200;
    float cellSize    = 10.0f;
    float amplitude   = 40.0f;
};

// Map layer visibility, opacity and order, in stack order (bottom first).
// Absent from scenarios written before layers existed; the stack falls back to
// its defaults in that case.
struct ScenarioLayerState {
    std::string id;
    bool visible = false;
    float opacity = 1.0f;
};

// A canopy import, recorded as the request that produced it rather than as the
// raster it produced.
//
// The raster is megabytes; it already lives in the portable tile cache, keyed
// by the request. Re-issuing the request on open finds it there, so a scenario
// reopened offline still gets back the vegetation it was solved with - and a
// scenario file stays a few kilobytes of text that a person can read.
//
// The substitution policy is part of the record because it changes the answer:
// the same fetch with a different developed-land policy is a different canopy.
struct CanopyImport {
    bool        enabled = false;
    std::string sourceId;                    // "landfire-LF2024"
    std::string developedPolicy = "assume";  // assume | source | bare
    float       assumedDevelopedCanopyM = 10.0f;
    std::string retrievedUtc;
};

struct ScenarioDoc {
    TerrainSource        terrain;
    SimEngine::Mode      mode = SimEngine::Mode::Fast;
    ReceiverSettings     receiver;
    CoverageCalibrationSettings calibration;
    std::string          calibrationSourceName;
    std::size_t          calibrationPairedSamples = 0;
    float                calibrationRmseBeforeDb =
        std::numeric_limits<float>::quiet_NaN();
    float                calibrationRmseAfterDb =
        std::numeric_limits<float>::quiet_NaN();
    std::vector<Antenna> antennas;
    std::vector<ScenarioLayerState> layers;
    // Hand-drawn vegetation, stored as areas rather than the raster they
    // produce: a megabyte of canopy grid would not survive a terrain change and
    // could not be edited afterwards.
    std::vector<VegetationArea> vegetation;
    CanopyImport canopyImport;
};

class ScenarioIO {
public:
    static void save(const ScenarioDoc& doc, const std::string& path);
    static ScenarioDoc load(const std::string& path);
};
