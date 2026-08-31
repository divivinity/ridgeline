#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct Antenna {
    glm::vec3   position;   // world XYZ; Y is absolute terrain elevation + AGL
    float       frequencyMHz = 150.0f;
    float       powerW       = 50.0f;
    float       gainDbi      = 3.0f;
    bool        isJammer     = false;
    std::string antennaType  = "omni";

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
    float cellSize = 10.0f;  // metres per cell
    glm::vec2 originXZ{0.0f, 0.0f};  // world XZ of cell (0,0)
    std::vector<float> dbm;  // [y*width+x], NaN = no signal

    float at(int x, int y) const { return dbm[y * width + x]; }
    bool  valid() const { return !dbm.empty(); }
};
