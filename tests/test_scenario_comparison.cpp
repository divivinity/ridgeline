#include "sim/ScenarioComparison.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL: " #c " line " << __LINE__ << '\n'; ++failures; } } while (0)
#define NEAR(a,b,t) CHECK(std::abs((double)(a)-(double)(b)) <= (double)(t))

FieldGrid grid(int width = 2, int height = 2) {
    FieldGrid result;
    result.width = width;
    result.height = height;
    result.cellSize = 25.0f;
    result.originXZ = {10.0f, 20.0f};
    result.maxXZ = {35.0f, 45.0f};
    const std::size_t count = result.cellCount();
    result.dbm.assign(count, -90.0f);
    result.jammerDbm.assign(count, -110.0f);
    result.snrDb.assign(count, 20.0f);
    result.sinrDb.assign(count, 15.0f);
    result.jOverSDb.assign(count, -20.0f);
    result.fadeMarginDb.assign(count, 10.0f);
    result.strongestDesiredDbm.assign(count, -91.0f);
    result.service.assign(count, 0);
    return result;
}

void checkIncompatible(const FieldGrid& baseline, const FieldGrid& current) {
    const ScenarioComparisonResult comparison =
        ScenarioComparison::compare(baseline, current);
    CHECK(!comparison.compatible);
    CHECK(!comparison.reason.empty());
    CHECK(comparison.totalCells == 0);
    CHECK(comparison.serviceTransition.empty());
}

void testCountsPercentagesAndTransitions() {
    FieldGrid baseline = grid();
    FieldGrid current = grid();
    baseline.service = {1, 2, 0, 0};
    current.service = {1, 0, 1, 0};

    const ScenarioComparisonResult comparison =
        ScenarioComparison::compare(baseline, current);
    CHECK(comparison.compatible);
    CHECK(comparison.reason.empty());
    CHECK(comparison.totalCells == 4);
    CHECK(comparison.baselineServed == 2);
    CHECK(comparison.currentServed == 2);
    CHECK(comparison.gained == 1);
    CHECK(comparison.lost == 1);
    CHECK(comparison.retained == 1);
    CHECK(comparison.unchanged == 1);
    NEAR(comparison.baselineServicePercent, 50.0, 1e-12);
    NEAR(comparison.currentServicePercent, 50.0, 1e-12);
    CHECK(comparison.serviceTransition.size() == 4);
    CHECK(comparison.serviceTransition[0] == 0);
    CHECK(comparison.serviceTransition[1] == -1);
    CHECK(comparison.serviceTransition[2] == 1);
    CHECK(comparison.serviceTransition[3] == 0);
}

void testFinitePairMetricMeans() {
    FieldGrid baseline = grid();
    FieldGrid current = grid();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    baseline.dbm = {1.0f, 2.0f, nan, 4.0f};
    current.dbm = {2.0f, 4.0f, 6.0f, inf};
    baseline.sinrDb = {10.0f, -inf, 20.0f, 30.0f};
    current.sinrDb = {11.0f, 5.0f, nan, 27.0f};
    baseline.fadeMarginDb = {5.0f, 6.0f, 7.0f, 8.0f};
    current.fadeMarginDb = {4.0f, 6.0f, 10.0f, 12.0f};

    const ScenarioComparisonResult comparison =
        ScenarioComparison::compare(baseline, current);
    CHECK(comparison.compatible);
    CHECK(comparison.desiredDeltaSamples == 2);
    NEAR(comparison.meanDesiredDeltaDb, 1.5, 1e-12);
    CHECK(comparison.sinrDeltaSamples == 2);
    NEAR(comparison.meanSinrDeltaDb, -1.0, 1e-12);
    CHECK(comparison.fadeMarginDeltaSamples == 4);
    NEAR(comparison.meanFadeMarginDeltaDb, 1.5, 1e-12);
}

void testNoFinitePairsProducesNaN() {
    FieldGrid baseline = grid(1, 1);
    FieldGrid current = grid(1, 1);
    baseline.maxXZ = current.maxXZ = {10.0f, 20.0f};
    baseline.dbm[0] = -std::numeric_limits<float>::infinity();
    current.dbm[0] = std::numeric_limits<float>::quiet_NaN();
    baseline.sinrDb[0] = std::numeric_limits<float>::quiet_NaN();
    current.sinrDb[0] = 1.0f;
    baseline.fadeMarginDb[0] = std::numeric_limits<float>::infinity();
    current.fadeMarginDb[0] = -1.0f;

    const ScenarioComparisonResult comparison =
        ScenarioComparison::compare(baseline, current);
    CHECK(comparison.compatible);
    CHECK(comparison.desiredDeltaSamples == 0);
    CHECK(comparison.sinrDeltaSamples == 0);
    CHECK(comparison.fadeMarginDeltaSamples == 0);
    CHECK(std::isnan(comparison.meanDesiredDeltaDb));
    CHECK(std::isnan(comparison.meanSinrDeltaDb));
    CHECK(std::isnan(comparison.meanFadeMarginDeltaDb));
}

void testRejectsInvalidDimensionsAndGeometry() {
    const FieldGrid valid = grid();
    FieldGrid candidate = valid;
    candidate.width = 0;
    checkIncompatible(candidate, valid);

    candidate = valid;
    candidate.height = -1;
    checkIncompatible(valid, candidate);

    candidate = valid;
    candidate.cellSize = 0.0f;
    checkIncompatible(candidate, valid);

    candidate = valid;
    candidate.originXZ.x = std::numeric_limits<float>::quiet_NaN();
    checkIncompatible(valid, candidate);

    candidate = valid;
    candidate.maxXZ.y = std::numeric_limits<float>::infinity();
    checkIncompatible(candidate, valid);

    candidate = valid;
    candidate.maxXZ.x = -1.0f;
    checkIncompatible(valid, candidate);
}

void testRequiresExactMatchingGeometry() {
    const FieldGrid baseline = grid();
    FieldGrid current = baseline;
    current.cellSize = std::nextafter(current.cellSize, 100.0f);
    checkIncompatible(baseline, current);

    current = baseline;
    current.originXZ.y = std::nextafter(current.originXZ.y, 100.0f);
    checkIncompatible(baseline, current);

    current = baseline;
    current.maxXZ.x = std::nextafter(current.maxXZ.x, 100.0f);
    checkIncompatible(baseline, current);

    current = grid(1, 4);
    current.maxXZ = baseline.maxXZ;
    checkIncompatible(baseline, current);
}

void testRejectsEveryMalformedMetricVector() {
    const FieldGrid valid = grid();
    for (int metric = 0; metric < 8; ++metric) {
        FieldGrid malformed = valid;
        switch (metric) {
        case 0: malformed.dbm.pop_back(); break;
        case 1: malformed.jammerDbm.pop_back(); break;
        case 2: malformed.snrDb.pop_back(); break;
        case 3: malformed.sinrDb.pop_back(); break;
        case 4: malformed.jOverSDb.pop_back(); break;
        case 5: malformed.fadeMarginDb.pop_back(); break;
        case 6: malformed.strongestDesiredDbm.pop_back(); break;
        case 7: malformed.service.pop_back(); break;
        }
        checkIncompatible(valid, malformed);
    }
}

} // namespace

int main() {
    testCountsPercentagesAndTransitions();
    testFinitePairMetricMeans();
    testNoFinitePairsProducesNaN();
    testRejectsInvalidDimensionsAndGeometry();
    testRequiresExactMatchingGeometry();
    testRejectsEveryMalformedMetricVector();
    if (failures == 0)
        std::cout << "Scenario comparison tests passed.\n";
    return failures == 0 ? 0 : 1;
}
