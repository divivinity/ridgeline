#pragma once

#include "sim/Types.h"
#include <string>

class AntennaPatternIO {
public:
    // CSV rows: plane,angle_deg,attenuation_db where plane is azimuth or
    // elevation. Two-column angle,attenuation files are accepted as azimuth.
    // Imported values are resampled to 1-degree attenuation tables.
    static void loadCsv(const std::string& path, Antenna& antenna);
};
