#pragma once
#include <string>
#include "sim/Types.h"

// Static helpers to export a FieldGrid to file.
// Both methods return true on success, false on write failure.
// Both methods return false immediately if the grid is not valid.
class Exporter {
public:
    // Write a false-color PNG: strong signal (high dBm) = green,
    // medium = yellow, weak = red. NaN cells = transparent black.
    // Requires Qt (QImage). Not available in the test binary.
    static bool exportPng(const FieldGrid& grid, const std::string& path);

    // Write a CSV file with header "x_m,z_m,dbm" and one row per cell.
    // NaN cells are written as the literal string "nan".
    static bool exportCsv(const FieldGrid& grid, const std::string& path);
};
