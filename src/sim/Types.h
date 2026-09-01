#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <glm/glm.hpp>

struct ReceiverSettings {
    float sensitivityDbm = -100.0f;
    float bandwidthHz = 25000.0f;
    float noiseFigureDb = 5.0f;
    float requiredSnrDb = 10.0f;
    float fadeMarginTargetDb = 10.0f;
    std::string polarization = "vertical";
};

struct Antenna {
    glm::vec3   position;   // world XYZ; Y is absolute terrain elevation + AGL
    float       frequencyMHz = 150.0f;
    float       powerW       = 50.0f;
    float       gainDbi      = 3.0f;
    bool        isJammer     = false;
    std::string antennaType  = "omni";

    // Feed and radiation-pattern parameters. Azimuth is clockwise from world
    // +Z (north); positive downtilt points the boresight below horizontal.
    float       azimuthDeg = 0.0f;
    float       downtiltDeg = 0.0f;
    float       horizontalBeamwidthDeg = 360.0f;
    float       verticalBeamwidthDeg = 90.0f;
    float       frontToBackDb = 30.0f;
    float       cableLossDb = 0.0f;
    std::string polarization = "vertical";

    // Optional imported attenuation patterns in dB. Azimuth samples are evenly
    // distributed over [0,360); elevation samples over [-90,+90]. Empty arrays
    // use the analytic beamwidth/front-to-back model above.
    std::vector<float> azimuthPatternDb;
    std::vector<float> elevationPatternDb;
    std::string patternSource;

    // Optional authoritative geospatial position. When present, latitude/
    // longitude survive terrain-resolution and analysis-area changes and can be
    // reprojected back into world X/Z. heightAglM is stored separately so the
    // antenna follows the elevation model rather than preserving stale MSL Y.
    bool        hasGeoPosition = false;
    double      latitudeDeg    = 0.0;
    double      longitudeDeg   = 0.0;
    float       heightAglM     = 10.0f;
};

struct FieldGrid {
    int   width    = 0;
    int   height   = 0;
    float cellSize = 10.0f;
    glm::vec2 originXZ{0.0f, 0.0f};
    glm::vec2 maxXZ{
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity()};

    // Desired received signal remains the primary/backwards-compatible field.
    std::vector<float> dbm;
    std::vector<float> jammerDbm;
    std::vector<float> snrDb;
    std::vector<float> sinrDb;
    std::vector<float> jOverSDb;
    std::vector<float> fadeMarginDb;
    std::vector<float> strongestDesiredDbm;
    std::vector<std::uint8_t> service;
    // Winning contributors for result inspection. Indices refer to the
    // scenario antenna vector; -1 means no in-band contributor was present.
    std::vector<std::int32_t> servingSourceIndex;
    std::vector<std::int32_t> strongestJammerIndex;

    float noiseFloorDbm = -120.0f;
    float receiverSensitivityDbm = -100.0f;
    float requiredSnrDb = 10.0f;

    float at(int x, int y) const { return dbm[y * width + x]; }
    glm::vec2 cellCenter(int x, int z) const {
        return {
            std::min(originXZ.x + static_cast<float>(x) * cellSize, maxXZ.x),
            std::min(originXZ.y + static_cast<float>(z) * cellSize, maxXZ.y)
        };
    }
    bool  valid() const { return !dbm.empty(); }
    std::size_t cellCount() const { return static_cast<std::size_t>(width) * height; }
};
