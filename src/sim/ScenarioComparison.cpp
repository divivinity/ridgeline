#include "sim/ScenarioComparison.h"

#include <cmath>
#include <limits>
#include <string>

namespace {

bool checkedCellCount(const FieldGrid& grid, std::size_t& count) {
    if (grid.width <= 0 || grid.height <= 0) return false;
    const auto width = static_cast<std::size_t>(grid.width);
    const auto height = static_cast<std::size_t>(grid.height);
    if (width > std::numeric_limits<std::size_t>::max() / height) return false;
    count = width * height;
    return true;
}

bool hasFiniteGeometry(const FieldGrid& grid) {
    return std::isfinite(grid.cellSize) && grid.cellSize > 0.0f &&
           std::isfinite(grid.originXZ.x) && std::isfinite(grid.originXZ.y) &&
           std::isfinite(grid.maxXZ.x) && std::isfinite(grid.maxXZ.y) &&
           grid.maxXZ.x >= 0.0f && grid.maxXZ.y >= 0.0f;
}

bool metricSizesMatchCellCount(const FieldGrid& grid, std::size_t count) {
    return grid.dbm.size() == count &&
           grid.jammerDbm.size() == count &&
           grid.snrDb.size() == count &&
           grid.sinrDb.size() == count &&
           grid.jOverSDb.size() == count &&
           grid.fadeMarginDb.size() == count &&
           grid.strongestDesiredDbm.size() == count &&
           grid.service.size() == count;
}

bool validateGrid(const FieldGrid& grid, const char* label,
                  std::string& reason, std::size_t& count) {
    if (!checkedCellCount(grid, count)) {
        reason = std::string(label) + " grid has invalid dimensions.";
        return false;
    }
    if (!hasFiniteGeometry(grid)) {
        reason = std::string(label) + " grid has invalid geometry.";
        return false;
    }
    if (!metricSizesMatchCellCount(grid, count)) {
        reason = std::string(label) +
                 " grid metric sizes do not match its dimensions.";
        return false;
    }
    return true;
}

bool sameGeometry(const FieldGrid& a, const FieldGrid& b) {
    return a.width == b.width && a.height == b.height &&
           a.cellSize == b.cellSize &&
           a.originXZ.x == b.originXZ.x &&
           a.originXZ.y == b.originXZ.y &&
           a.maxXZ.x == b.maxXZ.x && a.maxXZ.y == b.maxXZ.y;
}

struct MeanAccumulator {
    double sum = 0.0;
    std::size_t samples = 0;

    void add(float baseline, float current) {
        if (!std::isfinite(baseline) || !std::isfinite(current)) return;
        sum += static_cast<double>(current) - static_cast<double>(baseline);
        ++samples;
    }

    double mean() const {
        return samples > 0
                   ? sum / static_cast<double>(samples)
                   : std::numeric_limits<double>::quiet_NaN();
    }
};

} // namespace

namespace ScenarioComparison {

ScenarioComparisonResult compare(const FieldGrid& baseline,
                                 const FieldGrid& current) {
    ScenarioComparisonResult result;
    std::size_t baselineCells = 0;
    std::size_t currentCells = 0;
    if (!validateGrid(baseline, "Baseline", result.reason, baselineCells) ||
        !validateGrid(current, "Current", result.reason, currentCells)) {
        return result;
    }
    if (!sameGeometry(baseline, current)) {
        result.reason =
            "Coverage grids use different geometry; rerun both at the same "
            "location and analysis quality.";
        return result;
    }
    // This is implied by equal dimensions, but keeping the guard makes the
    // invariant explicit if FieldGrid changes in the future.
    if (baselineCells != currentCells) {
        result.reason = "Coverage grids contain different cell counts.";
        return result;
    }

    result.compatible = true;
    result.totalCells = baselineCells;
    result.serviceTransition.assign(baselineCells, 0);

    MeanAccumulator desired;
    MeanAccumulator sinr;
    MeanAccumulator fadeMargin;
    for (std::size_t i = 0; i < baselineCells; ++i) {
        const bool wasServed = baseline.service[i] != 0;
        const bool isServed = current.service[i] != 0;
        result.baselineServed += wasServed ? 1u : 0u;
        result.currentServed += isServed ? 1u : 0u;

        if (wasServed && isServed) {
            ++result.retained;
        } else if (wasServed) {
            ++result.lost;
            result.serviceTransition[i] = -1;
        } else if (isServed) {
            ++result.gained;
            result.serviceTransition[i] = 1;
        } else {
            ++result.unchanged;
        }

        desired.add(baseline.dbm[i], current.dbm[i]);
        sinr.add(baseline.sinrDb[i], current.sinrDb[i]);
        fadeMargin.add(baseline.fadeMarginDb[i], current.fadeMarginDb[i]);
    }

    const double denominator = static_cast<double>(baselineCells);
    result.baselineServicePercent =
        100.0 * static_cast<double>(result.baselineServed) / denominator;
    result.currentServicePercent =
        100.0 * static_cast<double>(result.currentServed) / denominator;

    result.meanDesiredDeltaDb = desired.mean();
    result.meanSinrDeltaDb = sinr.mean();
    result.meanFadeMarginDeltaDb = fadeMargin.mean();
    result.desiredDeltaSamples = desired.samples;
    result.sinrDeltaSamples = sinr.samples;
    result.fadeMarginDeltaSamples = fadeMargin.samples;
    return result;
}

} // namespace ScenarioComparison
