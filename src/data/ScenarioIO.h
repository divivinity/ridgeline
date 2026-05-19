#pragma once
#include <string>
#include <vector>
#include "sim/Types.h"
#include "sim/SimEngine.h"

struct TerrainSource {
    enum class Type { Hills, Flat, Hgt };

    Type        type         = Type::Hills;
    std::string hgtPath;            // only for Hgt
    float       hgtLat       = 35.05f;
    float       hgtLon       = -78.88f;
    int         hgtSize      = 512;

    // Procedural terrain dimensions (Hills + Flat).
    int   widthCells  = 200;
    int   heightCells = 200;
    float cellSize    = 10.0f;
    float amplitude   = 40.0f;  // Hills only; ignored for Flat
};

struct ScenarioDoc {
    TerrainSource        terrain;
    SimEngine::Mode      mode = SimEngine::Mode::Fast;
    std::vector<Antenna> antennas;
};

class ScenarioIO {
public:
    // Serialize doc to a JSON file at path.
    // Throws std::runtime_error on failure.
    static void save(const ScenarioDoc& doc, const std::string& path);

    // Deserialize from a JSON file at path.
    // Throws std::runtime_error on failure.
    static ScenarioDoc load(const std::string& path);
};
