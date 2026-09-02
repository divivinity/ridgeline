#pragma once

#include "sim/TerrainModel.h"
#include "sim/Types.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

enum class MeasurementCoordinateKind {
    Wgs84,
    LocalXZ
};

// One measured desired-signal sample. Exactly one coordinate representation is
// authoritative, as selected by coordinateKind. Optional numeric metadata uses
// NaN when it was not supplied by the source file.
struct RFMeasurement {
    MeasurementCoordinateKind coordinateKind = MeasurementCoordinateKind::Wgs84;
    double latitudeDeg = std::numeric_limits<double>::quiet_NaN();
    double longitudeDeg = std::numeric_limits<double>::quiet_NaN();
    double xM = std::numeric_limits<double>::quiet_NaN();
    double zM = std::numeric_limits<double>::quiet_NaN();
    double measuredDbm = std::numeric_limits<double>::quiet_NaN();
    double frequencyMHz = std::numeric_limits<double>::quiet_NaN();
    double heightAglM = std::numeric_limits<double>::quiet_NaN();
    std::string label;
    std::size_t sourceLine = 0;
};

struct CalibrationPair {
    std::size_t measurementIndex = 0;
    RFMeasurement measurement;
    double worldX = std::numeric_limits<double>::quiet_NaN();
    double worldZ = std::numeric_limits<double>::quiet_NaN();
    double predictedDbm = std::numeric_limits<double>::quiet_NaN();
    double measuredDbm = std::numeric_limits<double>::quiet_NaN();
    // measured minus predicted; adding the mean residual to predictions is the
    // least-squares constant correction.
    double residualDb = std::numeric_limits<double>::quiet_NaN();
    int gridX = -1;
    int gridZ = -1;
    int servingSourceIndex = -1;
};

struct CalibrationReport {
    std::size_t imported = 0;
    std::size_t insideArea = 0;
    std::size_t paired = 0;
    std::size_t rejected = 0;
    std::size_t outsideArea = 0;
    std::size_t noPrediction = 0;

    double meanBiasDb = std::numeric_limits<double>::quiet_NaN();
    double medianBiasDb = std::numeric_limits<double>::quiet_NaN();
    double maeDb = std::numeric_limits<double>::quiet_NaN();
    double maeAfterBiasDb = std::numeric_limits<double>::quiet_NaN();
    double rmseBeforeDb = std::numeric_limits<double>::quiet_NaN();
    double rmseAfterBiasDb = std::numeric_limits<double>::quiet_NaN();

    std::vector<CalibrationPair> pairs;
    std::vector<std::string> warnings;

    bool canApply() const {
        return paired >= 3 && std::isfinite(meanBiasDb);
    }
};

struct MeasurementCalibrationOptions {
    double expectedReceiverHeightAglM = 1.5;
    double heightToleranceM = 0.25;
    double frequencyToleranceMHz = 0.05;
};

namespace MeasurementCalibration {

// Pairs measurements with the desired-power grid. Coordinates outside the
// terrain AOI are rejected before sampling; grid values are never obtained by
// clamping an out-of-area location. Within the AOI, edge cells extend to the
// terrain boundary and finite bilinear neighbors are renormalized.
CalibrationReport evaluate(
    const std::vector<RFMeasurement>& measurements,
    const FieldGrid& grid,
    const TerrainModel& terrain,
    const std::vector<Antenna>& antennas = {},
    const MeasurementCalibrationOptions& options = {});

} // namespace MeasurementCalibration
