#include "data/MeasurementIO.h"
#include "sim/MeasurementCalibration.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL: " #c " line " << __LINE__ << '\n'; ++failures; } } while (0)
#define NEAR(a,b,t) CHECK(std::abs((double)(a)-(double)(b)) <= (double)(t))

TerrainModel terrain(bool georeferenced = true) {
    TerrainModel result;
    result.width = 3;
    result.height = 3;
    result.setCellSpacing(10.0f, 10.0f);
    result.heights.assign(9, 0.0f);
    if (georeferenced)
        result.setGeoReference(35.0, -80.0, 0.01, 0.01);
    return result;
}

FieldGrid grid() {
    FieldGrid result;
    result.width = 2;
    result.height = 2;
    result.cellSize = 10.0f;
    result.originXZ = {5.0f, 5.0f};
    result.maxXZ = {20.0f, 20.0f};
    result.dbm = {-100.0f, -90.0f, -80.0f, -70.0f};
    result.servingSourceIndex = {0, 1, 2, 3};
    return result;
}

RFMeasurement local(double x, double z, double measuredDbm) {
    RFMeasurement result;
    result.coordinateKind = MeasurementCoordinateKind::LocalXZ;
    result.xM = x;
    result.zM = z;
    result.measuredDbm = measuredDbm;
    return result;
}

bool warningContains(const CalibrationReport& report, const std::string& text) {
    for (const std::string& warning : report.warnings) {
        if (warning.find(text) != std::string::npos) return true;
    }
    return false;
}

void testQuotedCaseInsensitiveCsv() {
    std::istringstream csv(
        "\xEF\xBB\xBFLATITUDE,Longitude,MEASURED_DBM,Frequency_MHz,Height_AGL_M,Label\r\n"
        "35.01,-79.99,-87.25,155.5,1.5,\"route, \"\"alpha\"\"\"\r\n"
        "35.02,-79.98,-88,,,\"north\nroute\"\r\n");
    const MeasurementLoadResult loaded = MeasurementIO::parseCsv(csv);
    CHECK(loaded.errors.empty());
    CHECK(loaded.rowsRead == 2);
    CHECK(loaded.rejectedRows == 0);
    CHECK(loaded.measurements.size() == 2);
    CHECK(loaded.measurements[0].coordinateKind == MeasurementCoordinateKind::Wgs84);
    NEAR(loaded.measurements[0].measuredDbm, -87.25, 1e-12);
    NEAR(loaded.measurements[0].frequencyMHz, 155.5, 1e-12);
    NEAR(loaded.measurements[0].heightAglM, 1.5, 1e-12);
    CHECK(loaded.measurements[0].label == "route, \"alpha\"");
    CHECK(loaded.measurements[0].sourceLine == 2);
    CHECK(loaded.measurements[1].label == "north\nroute");
    CHECK(loaded.measurements[1].sourceLine == 3);
}

void testLocalAndMixedCoordinateCsv() {
    std::istringstream localCsv(
        "x_m,z_m,measured_dbm,label\n"
        "12.5,8,-91,local point\n");
    const MeasurementLoadResult localLoaded = MeasurementIO::parseCsv(localCsv);
    CHECK(localLoaded.errors.empty());
    CHECK(localLoaded.measurements.size() == 1);
    CHECK(localLoaded.measurements[0].coordinateKind == MeasurementCoordinateKind::LocalXZ);
    NEAR(localLoaded.measurements[0].xM, 12.5, 1e-12);

    std::istringstream mixedCsv(
        "latitude,longitude,x_m,z_m,measured_dbm\n"
        "35,-80,,,-90\n"
        ",,5,6,-91\n"
        "35,-80,5,6,-92\n");
    const MeasurementLoadResult mixed = MeasurementIO::parseCsv(mixedCsv);
    CHECK(mixed.measurements.size() == 2);
    CHECK(mixed.measurements[0].coordinateKind == MeasurementCoordinateKind::Wgs84);
    CHECK(mixed.measurements[1].coordinateKind == MeasurementCoordinateKind::LocalXZ);
    CHECK(mixed.rejectedRows == 1);
    CHECK(mixed.errors.size() == 1);
    CHECK(mixed.errors[0].line == 4);
}

void testCsvDiagnosticsAndValidation() {
    std::istringstream csv(
        "latitude,longitude,measured_dbm,frequency_mhz,height_agl_m\n"
        "91,-80,-90,,\n"
        "35,-181,-90,,\n"
        "35,-80,nan,,\n"
        "35,-80,101,,\n"
        "35,-80,-90,-1,\n"
        "35,-80,-90,150,-2\n"
        "35,-80,-90,150,1.5\n");
    const MeasurementLoadResult loaded = MeasurementIO::parseCsv(csv);
    CHECK(loaded.rowsRead == 7);
    CHECK(loaded.rejectedRows == 6);
    CHECK(loaded.errors.size() == 6);
    CHECK(loaded.measurements.size() == 1);
    CHECK(loaded.errors.front().line == 2);
    CHECK(loaded.errors.back().line == 7);

    std::istringstream badHeader("latitude,measured_dbm\n35,-90\n");
    const MeasurementLoadResult bad = MeasurementIO::parseCsv(badHeader);
    CHECK(!bad.errors.empty());
    CHECK(bad.measurements.empty());

    std::istringstream badQuote(
        "x_m,z_m,measured_dbm,label\n1,2,-90,\"unterminated\n");
    const MeasurementLoadResult quoteResult = MeasurementIO::parseCsv(badQuote);
    CHECK(quoteResult.rejectedRows == 1);
    CHECK(!quoteResult.errors.empty());
    CHECK(quoteResult.errors[0].line == 2);
}

void testProjectionInterpolationAndMetadata() {
    RFMeasurement measurement;
    measurement.coordinateKind = MeasurementCoordinateKind::Wgs84;
    measurement.latitudeDeg = 35.01;
    measurement.longitudeDeg = -79.99;
    measurement.measuredDbm = -84.0;

    std::vector<Antenna> antennas(4);
    const CalibrationReport report = MeasurementCalibration::evaluate(
        {measurement}, grid(), terrain(), antennas);
    CHECK(report.imported == 1);
    CHECK(report.insideArea == 1);
    CHECK(report.paired == 1);
    CHECK(report.rejected == 0);
    CHECK(report.pairs.size() == 1);
    NEAR(report.pairs[0].worldX, 10.0, 2e-4);
    NEAR(report.pairs[0].worldZ, 10.0, 2e-4);
    NEAR(report.pairs[0].predictedDbm, -85.0, 2e-4);
    NEAR(report.pairs[0].residualDb, 1.0, 2e-4);
    CHECK(report.pairs[0].gridX == 1);
    CHECK(report.pairs[0].gridZ == 1);
    CHECK(report.pairs[0].servingSourceIndex == 3);
}

void testFiniteNeighborAndEdgeBehavior() {
    FieldGrid field = grid();
    field.dbm[3] = std::numeric_limits<float>::quiet_NaN();
    const CalibrationReport middle = MeasurementCalibration::evaluate(
        {local(10.0, 10.0, -90.0)}, field, terrain(false));
    CHECK(middle.paired == 1);
    NEAR(middle.pairs[0].predictedDbm, -90.0, 1e-12);

    const CalibrationReport missingCenter = MeasurementCalibration::evaluate(
        {local(15.0, 15.0, -70.0)}, field, terrain(false));
    CHECK(missingCenter.insideArea == 1);
    CHECK(missingCenter.paired == 0);
    CHECK(missingCenter.noPrediction == 1);

    const CalibrationReport edges = MeasurementCalibration::evaluate(
        {local(0.0, 0.0, -100.0), local(-0.001, 0.0, -100.0),
         local(20.001, 10.0, -80.0)},
        grid(), terrain(false));
    CHECK(edges.insideArea == 1);
    CHECK(edges.paired == 1);
    CHECK(edges.outsideArea == 2);
    CHECK(edges.rejected == 2);
    NEAR(edges.pairs[0].predictedDbm, -100.0, 1e-12);
}

void testExactCalibrationStatisticsAndWarnings() {
    std::vector<RFMeasurement> measurements = {
        local(5.0, 5.0, -99.0),
        local(15.0, 5.0, -88.0),
        local(5.0, 15.0, -77.0),
        local(15.0, 15.0, -66.0)};
    measurements[0].frequencyMHz = 160.0;
    measurements[0].heightAglM = 3.0;

    std::vector<Antenna> antennas(4);
    for (Antenna& antenna : antennas) antenna.frequencyMHz = 150.0f;
    const CalibrationReport report = MeasurementCalibration::evaluate(
        measurements, grid(), terrain(false), antennas);
    CHECK(report.paired == 4);
    CHECK(report.canApply());
    NEAR(report.meanBiasDb, 2.5, 1e-12);
    NEAR(report.medianBiasDb, 2.5, 1e-12);
    NEAR(report.maeDb, 2.5, 1e-12);
    NEAR(report.rmseBeforeDb, std::sqrt(7.5), 1e-12);
    NEAR(report.maeAfterBiasDb, 1.0, 1e-12);
    NEAR(report.rmseAfterBiasDb, std::sqrt(1.25), 1e-12);
    CHECK(warningContains(report, "Fewer than 10"));
    CHECK(warningContains(report, "frequency"));
    CHECK(warningContains(report, "receiver height"));
}

void testInsufficientSamplesAndInvalidInputs() {
    const CalibrationReport tooSmall = MeasurementCalibration::evaluate(
        {local(5.0, 5.0, -99.0), local(15.0, 5.0, -88.0)},
        grid(), terrain(false));
    CHECK(!tooSmall.canApply());
    CHECK(warningContains(tooSmall, "Fewer than 3"));
    CHECK(warningContains(tooSmall, "Fewer than 10"));

    RFMeasurement wgs;
    wgs.coordinateKind = MeasurementCoordinateKind::Wgs84;
    wgs.latitudeDeg = 35.0;
    wgs.longitudeDeg = -80.0;
    wgs.measuredDbm = -90.0;
    const CalibrationReport noReference = MeasurementCalibration::evaluate(
        {wgs}, grid(), terrain(false));
    CHECK(noReference.paired == 0);
    CHECK(noReference.rejected == 1);
    CHECK(warningContains(noReference, "georeferenced terrain"));

    FieldGrid invalid = grid();
    invalid.dbm.pop_back();
    const CalibrationReport invalidGrid = MeasurementCalibration::evaluate(
        {local(5.0, 5.0, -90.0)}, invalid, terrain(false));
    CHECK(invalidGrid.rejected == 1);
    CHECK(warningContains(invalidGrid, "unavailable or invalid"));
}

void testAntimeridianProjection() {
    TerrainModel seam;
    seam.width = 5;
    seam.height = 5;
    seam.setCellSpacing(10.0f, 10.0f);
    seam.heights.assign(25, 0.0f);
    seam.setGeoReference(10.0, 179.8, 0.01, 0.1);

    FieldGrid field;
    field.width = 4;
    field.height = 4;
    field.cellSize = 10.0f;
    field.originXZ = {5.0f, 5.0f};
    field.maxXZ = {40.0f, 40.0f};
    field.dbm.assign(16, -90.0f);

    RFMeasurement measurement;
    measurement.coordinateKind = MeasurementCoordinateKind::Wgs84;
    measurement.latitudeDeg = 10.01;
    measurement.longitudeDeg = -179.9;
    measurement.measuredDbm = -89.0;
    const CalibrationReport report = MeasurementCalibration::evaluate(
        {measurement}, field, seam);
    CHECK(report.paired == 1);
    NEAR(report.pairs[0].worldX, 30.0, 5e-4);
    NEAR(report.pairs[0].worldZ, 10.0, 5e-4);
}

} // namespace

int main() {
    testQuotedCaseInsensitiveCsv();
    testLocalAndMixedCoordinateCsv();
    testCsvDiagnosticsAndValidation();
    testProjectionInterpolationAndMetadata();
    testFiniteNeighborAndEdgeBehavior();
    testExactCalibrationStatisticsAndWarnings();
    testInsufficientSamplesAndInvalidInputs();
    testAntimeridianProjection();
    if (failures == 0)
        std::cout << "Measurement calibration tests passed.\n";
    return failures == 0 ? 0 : 1;
}
