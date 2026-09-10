#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <limits>
#include "sim/Types.h"
#include "sim/SimEngine.h"

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
};

class ScenarioIO {
public:
    static void save(const ScenarioDoc& doc, const std::string& path);
    static ScenarioDoc load(const std::string& path);
};
