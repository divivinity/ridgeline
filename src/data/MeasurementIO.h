#pragma once

#include "sim/MeasurementCalibration.h"

#include <cstddef>
#include <istream>
#include <string>
#include <vector>

struct MeasurementDiagnostic {
    std::size_t line = 0;
    std::string message;
};

struct MeasurementLoadResult {
    std::vector<RFMeasurement> measurements;
    std::vector<MeasurementDiagnostic> errors;
    std::vector<MeasurementDiagnostic> warnings;
    std::size_t rowsRead = 0;
    std::size_t rejectedRows = 0;

    bool hasErrors() const { return !errors.empty(); }
};

namespace MeasurementIO {

// Parses a header-driven CSV stream. Headers are case-insensitive. Files may
// contain either latitude/longitude or x_m/z_m (and may contain both for mixed
// rows), plus required measured_dbm and optional frequency_mhz, height_agl_m,
// and label columns.
MeasurementLoadResult parseCsv(std::istream& input);
MeasurementLoadResult loadCsv(const std::string& path);

} // namespace MeasurementIO
