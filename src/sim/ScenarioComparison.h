#pragma once

#include "sim/Types.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Summary of a cell-for-cell comparison between two coverage solutions.
// Delta values are current minus baseline and only include cells where both
// metric values are finite.
struct ScenarioComparisonResult {
    bool compatible = false;
    std::string reason;

    std::size_t totalCells = 0;
    std::size_t baselineServed = 0;
    std::size_t currentServed = 0;
    std::size_t gained = 0;
    std::size_t lost = 0;
    std::size_t retained = 0;
    std::size_t unchanged = 0; // unserved in both solutions

    double baselineServicePercent = 0.0;
    double currentServicePercent = 0.0;

    double meanDesiredDeltaDb = std::numeric_limits<double>::quiet_NaN();
    double meanSinrDeltaDb = std::numeric_limits<double>::quiet_NaN();
    double meanFadeMarginDeltaDb = std::numeric_limits<double>::quiet_NaN();
    std::size_t desiredDeltaSamples = 0;
    std::size_t sinrDeltaSamples = 0;
    std::size_t fadeMarginDeltaSamples = 0;

    // Per-cell service-state change: -1 lost, 0 unchanged, +1 gained.
    // Retained service and retained non-service both use zero.
    std::vector<std::int8_t> serviceTransition;
};

namespace ScenarioComparison {

// Exact geometry is required because this comparison intentionally performs no
// spatial resampling. An incompatible result contains a human-readable reason
// and no partial statistics.
ScenarioComparisonResult compare(const FieldGrid& baseline,
                                 const FieldGrid& current);

} // namespace ScenarioComparison
