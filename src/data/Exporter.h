#pragma once
#include <string>
#include "sim/Types.h"
#include "sim/TerrainModel.h"
class QImage;

// Static image/grid export helpers. Return false on invalid input or write failure.
class Exporter {
public:
    // Atomic PNG replacement; null images and failed writes preserve the target.
    static bool savePng(const QImage& image, const std::string& path);
    // Write a false-color PNG: strong signal (high dBm) = green,
    // medium = yellow, weak = red. NaN cells = transparent black.
    // Requires Qt (QImage).
    static bool exportPng(const FieldGrid& grid, const std::string& path);

    // Write coordinates and RF metrics, with a header and one row per cell.
    // NaN cells are written as the literal string "nan".
    static bool exportCsv(const FieldGrid& grid, const std::string& path);
    static bool exportCsv(const FieldGrid& grid, const TerrainModel& terrain,
                          const std::string& path);
};
