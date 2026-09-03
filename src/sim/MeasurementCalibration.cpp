#include "sim/MeasurementCalibration.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <utility>

namespace {

bool finiteGridGeometry(const FieldGrid& grid) {
    if (grid.width <= 0 || grid.height <= 0 ||
        !std::isfinite(grid.cellSize) || grid.cellSize <= 0.0f ||
        !std::isfinite(grid.originXZ.x) || !std::isfinite(grid.originXZ.y)) {
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(grid.width) *
                              static_cast<std::size_t>(grid.height);
    return grid.dbm.size() >= count;
}

bool measurementWorldPosition(const RFMeasurement& measurement,
                              const TerrainModel& terrain,
                              double& worldX,
                              double& worldZ) {
    if (measurement.coordinateKind == MeasurementCoordinateKind::Wgs84) {
        if (!terrain.hasGeoReference() ||
            !std::isfinite(measurement.latitudeDeg) ||
            !std::isfinite(measurement.longitudeDeg) ||
            measurement.latitudeDeg < -90.0 || measurement.latitudeDeg > 90.0 ||
            measurement.longitudeDeg < -180.0 || measurement.longitudeDeg > 180.0) {
            return false;
        }
        const glm::vec2 world = terrain.latLonToWorld(
            measurement.latitudeDeg, measurement.longitudeDeg);
        worldX = world.x;
        worldZ = world.y;
    } else {
        worldX = measurement.xM;
        worldZ = measurement.zM;
    }
    return std::isfinite(worldX) && std::isfinite(worldZ);
}

bool sampleDesiredPower(const FieldGrid& grid,
                        double worldX,
                        double worldZ,
                        double& predictedDbm) {
    double gx = (worldX - static_cast<double>(grid.originXZ.x)) /
                static_cast<double>(grid.cellSize);
    double gz = (worldZ - static_cast<double>(grid.originXZ.y)) /
                static_cast<double>(grid.cellSize);

    // The first and last result cells represent the half-cell strips between
    // their centers and the AOI edge. This clamp occurs only after the caller
    // has established that the point is inside the terrain AOI.
    gx = std::clamp(gx, 0.0, static_cast<double>(grid.width - 1));
    gz = std::clamp(gz, 0.0, static_cast<double>(grid.height - 1));

    const int x0 = static_cast<int>(std::floor(gx));
    const int z0 = static_cast<int>(std::floor(gz));
    const int x1 = std::min(x0 + 1, grid.width - 1);
    const int z1 = std::min(z0 + 1, grid.height - 1);
    const double tx = gx - static_cast<double>(x0);
    const double tz = gz - static_cast<double>(z0);

    const int xs[2] = {x0, x1};
    const int zs[2] = {z0, z1};
    const int xCount = x0 == x1 ? 1 : 2;
    const int zCount = z0 == z1 ? 1 : 2;
    double weightedSum = 0.0;
    double finiteWeight = 0.0;
    for (int zi = 0; zi < zCount; ++zi) {
        const int z = zs[zi];
        const double wz = z0 == z1 ? 1.0 : (z == z0 ? 1.0 - tz : tz);
        for (int xi = 0; xi < xCount; ++xi) {
            const int x = xs[xi];
            const double wx = x0 == x1 ? 1.0 : (x == x0 ? 1.0 - tx : tx);
            const double weight = wx * wz;
            if (weight <= 0.0) continue;
            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(grid.width) +
                                      static_cast<std::size_t>(x);
            const float value = grid.dbm[index];
            if (!std::isfinite(value)) continue;
            weightedSum += static_cast<double>(value) * weight;
            finiteWeight += weight;
        }
    }
    if (finiteWeight <= 0.0) return false;
    predictedDbm = weightedSum / finiteWeight;
    return std::isfinite(predictedDbm);
}

std::string countMessage(std::size_t count,
                         const char* singular,
                         const char* plural,
                         const char* suffix) {
    std::ostringstream message;
    message << count << ' ' << (count == 1 ? singular : plural) << suffix;
    return message.str();
}

} // namespace

namespace MeasurementCalibration {

CalibrationReport evaluate(
    const std::vector<RFMeasurement>& measurements,
    const FieldGrid& grid,
    const TerrainModel& terrain,
    const std::vector<Antenna>& antennas,
    const MeasurementCalibrationOptions& options) {
    CalibrationReport report;
    report.imported = measurements.size();

    const glm::vec3 terrainWorld = terrain.worldSize();
    const bool validTerrain = terrain.width > 0 && terrain.height > 0 &&
        std::isfinite(terrainWorld.x) && std::isfinite(terrainWorld.z) &&
        terrainWorld.x >= 0.0f && terrainWorld.z >= 0.0f;
    if (!finiteGridGeometry(grid) || !validTerrain) {
        report.rejected = report.imported;
        report.warnings.emplace_back(
            "Coverage results or terrain geometry are unavailable or invalid.");
        if (report.paired < 3) {
            report.warnings.emplace_back(
                "Fewer than 3 measurements were paired; a correction cannot be applied.");
        }
        if (report.paired < 10) {
            report.warnings.emplace_back(
                "Fewer than 10 measurements were paired; calibration confidence is limited.");
        }
        return report;
    }

    std::size_t wgsWithoutReference = 0;
    std::size_t invalidCoordinates = 0;
    std::size_t invalidMeasuredValues = 0;
    std::size_t frequencyMismatches = 0;
    std::size_t heightMismatches = 0;
    report.pairs.reserve(measurements.size());

    for (std::size_t measurementIndex = 0;
         measurementIndex < measurements.size(); ++measurementIndex) {
        const RFMeasurement& measurement = measurements[measurementIndex];
        double worldX = 0.0;
        double worldZ = 0.0;
        if (!measurementWorldPosition(measurement, terrain, worldX, worldZ)) {
            if (measurement.coordinateKind == MeasurementCoordinateKind::Wgs84 &&
                !terrain.hasGeoReference()) {
                ++wgsWithoutReference;
            } else {
                ++invalidCoordinates;
            }
            continue;
        }

        // Inclusive bounds deliberately admit measurements on the AOI edge.
        // Anything beyond an edge is rejected rather than sampled at that edge.
        if (worldX < 0.0 || worldZ < 0.0 ||
            worldX > static_cast<double>(terrainWorld.x) ||
            worldZ > static_cast<double>(terrainWorld.z)) {
            ++report.outsideArea;
            continue;
        }
        ++report.insideArea;

        double predictedDbm = 0.0;
        if (!std::isfinite(measurement.measuredDbm) ||
            measurement.measuredDbm < -300.0 || measurement.measuredDbm > 100.0) {
            ++invalidMeasuredValues;
            continue;
        }
        if (!sampleDesiredPower(grid, worldX, worldZ, predictedDbm)) {
            ++report.noPrediction;
            continue;
        }

        const double gx = (worldX - static_cast<double>(grid.originXZ.x)) /
                          static_cast<double>(grid.cellSize);
        const double gz = (worldZ - static_cast<double>(grid.originXZ.y)) /
                          static_cast<double>(grid.cellSize);
        const int nearestX = std::clamp(
            static_cast<int>(std::lround(gx)), 0, grid.width - 1);
        const int nearestZ = std::clamp(
            static_cast<int>(std::lround(gz)), 0, grid.height - 1);
        const std::size_t nearestIndex = static_cast<std::size_t>(nearestZ) *
            static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(nearestX);
        int servingSourceIndex = -1;
        if (nearestIndex < grid.servingSourceIndex.size())
            servingSourceIndex = grid.servingSourceIndex[nearestIndex];

        CalibrationPair pair;
        pair.measurementIndex = measurementIndex;
        pair.measurement = measurement;
        pair.worldX = worldX;
        pair.worldZ = worldZ;
        pair.predictedDbm = predictedDbm;
        pair.measuredDbm = measurement.measuredDbm;
        pair.residualDb = measurement.measuredDbm - predictedDbm;
        pair.gridX = nearestX;
        pair.gridZ = nearestZ;
        pair.servingSourceIndex = servingSourceIndex;
        report.pairs.push_back(std::move(pair));

        if (std::isfinite(measurement.frequencyMHz) &&
            servingSourceIndex >= 0 &&
            servingSourceIndex < static_cast<int>(antennas.size()) &&
            std::isfinite(antennas[servingSourceIndex].frequencyMHz) &&
            std::abs(measurement.frequencyMHz -
                     static_cast<double>(antennas[servingSourceIndex].frequencyMHz)) >
                std::max(0.0, options.frequencyToleranceMHz)) {
            ++frequencyMismatches;
        }
        if (std::isfinite(measurement.heightAglM) &&
            std::isfinite(options.expectedReceiverHeightAglM) &&
            std::abs(measurement.heightAglM - options.expectedReceiverHeightAglM) >
                std::max(0.0, options.heightToleranceM)) {
            ++heightMismatches;
        }
    }

    report.paired = report.pairs.size();
    report.rejected = report.imported - report.paired;

    if (!report.pairs.empty()) {
        std::vector<double> residuals;
        residuals.reserve(report.pairs.size());
        double sum = 0.0;
        double absSum = 0.0;
        double squareSum = 0.0;
        for (const CalibrationPair& pair : report.pairs) {
            residuals.push_back(pair.residualDb);
            sum += pair.residualDb;
            absSum += std::abs(pair.residualDb);
            squareSum += pair.residualDb * pair.residualDb;
        }
        const double count = static_cast<double>(residuals.size());
        report.meanBiasDb = sum / count;
        report.maeDb = absSum / count;
        report.rmseBeforeDb = std::sqrt(squareSum / count);

        std::sort(residuals.begin(), residuals.end());
        const std::size_t middle = residuals.size() / 2;
        report.medianBiasDb = residuals.size() % 2 != 0
            ? residuals[middle]
            : (residuals[middle - 1] + residuals[middle]) * 0.5;

        double correctedAbsSum = 0.0;
        double correctedSquareSum = 0.0;
        for (double residual : residuals) {
            const double corrected = residual - report.meanBiasDb;
            correctedAbsSum += std::abs(corrected);
            correctedSquareSum += corrected * corrected;
        }
        report.maeAfterBiasDb = correctedAbsSum / count;
        report.rmseAfterBiasDb = std::sqrt(correctedSquareSum / count);
    }

    if (report.paired < 3) {
        report.warnings.emplace_back(
            "Fewer than 3 measurements were paired; a correction cannot be applied.");
    }
    if (report.paired < 10) {
        report.warnings.emplace_back(
            "Fewer than 10 measurements were paired; calibration confidence is limited.");
    }
    if (report.outsideArea > 0) {
        report.warnings.push_back(countMessage(
            report.outsideArea, "measurement was", "measurements were",
            " outside the analysis area and rejected."));
    }
    if (report.noPrediction > 0) {
        report.warnings.push_back(countMessage(
            report.noPrediction, "measurement had", "measurements had",
            " no finite predicted signal and could not be paired."));
    }
    if (wgsWithoutReference > 0) {
        report.warnings.push_back(countMessage(
            wgsWithoutReference, "WGS84 measurement requires", "WGS84 measurements require",
            " georeferenced terrain and could not be projected."));
    }
    if (invalidCoordinates > 0) {
        report.warnings.push_back(countMessage(
            invalidCoordinates, "measurement had", "measurements had",
            " invalid coordinates and could not be paired."));
    }
    if (invalidMeasuredValues > 0) {
        report.warnings.push_back(countMessage(
            invalidMeasuredValues, "measurement had", "measurements had",
            " an invalid measured_dbm value and could not be paired."));
    }
    if (frequencyMismatches > 0) {
        report.warnings.push_back(countMessage(
            frequencyMismatches, "paired measurement uses", "paired measurements use",
            " a frequency that differs from its serving emitter."));
    }
    if (heightMismatches > 0) {
        report.warnings.push_back(countMessage(
            heightMismatches, "paired measurement uses", "paired measurements use",
            " a receiver height that differs from the coverage solve."));
    }

    return report;
}

} // namespace MeasurementCalibration
