#include "data/AntennaLibrary.h"
#include "ui/LocationDialog.h"
#include "ui/PropertiesPanel.h"
#include "ui/SourcePositionDialog.h"
#include "ui/PointAnalysisPanel.h"
#include "ui/ScenarioComparisonPanel.h"
#include "ui/CalibrationPanel.h"
#include "sim/TerrainModel.h"
#include "sim/ScenarioComparison.h"
#include "sim/MeasurementCalibration.h"
#include "data/Exporter.h"
#include "RuntimePaths.h"

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <functional>
#include <fstream>
#include <iostream>

namespace {

int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAIL: " #condition " (line " << __LINE__ << ")\n"; \
        ++failures; \
    } \
} while (0)

bool waitUntil(const std::function<bool()>& condition, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (condition()) return true;
    }
    return condition();
}

void testAdvancedPanelRemainsEditableAndCollapsible() {
    PropertiesPanel panel;
    AntennaLibrary library;
    library.loadDefaults();
    panel.setAntennaLibrary(&library);

    Antenna antenna;
    antenna.antennaType = "omni";
    panel.load(&antenna);
    panel.resize(320, 300);
    panel.show();
    QApplication::processEvents();

    auto* toggle = panel.findChild<QToolButton*>("AdvancedSettingsToggle");
    auto* advanced = panel.findChild<QWidget*>("AdvancedSettingsPanel");
    auto* type = panel.findChild<QComboBox*>("AntennaTypeCombo");
    auto* polarization = panel.findChild<QComboBox*>("PolarizationCombo");
    auto* editPosition = panel.findChild<QPushButton*>("EditSourcePositionButton");
    CHECK(toggle && advanced && type && polarization && editPosition);
    if (!toggle || !advanced || !type || !polarization || !editPosition) return;
    panel.setPositionSummary("35.000000°, -80.000000°\nMGRS 17SNA0000000000");
    CHECK(editPosition->isVisible());

    toggle->click();
    QApplication::processEvents();
    CHECK(toggle->isChecked());
    CHECK(advanced->isVisible());
    CHECK(toggle->isVisible());

    type->setCurrentText("yagi");
    polarization->setCurrentText("horizontal");
    QApplication::processEvents();
    CHECK(type->currentText() == "yagi");
    CHECK(polarization->currentText() == "horizontal");
    CHECK(toggle->isVisible());

    toggle->click();
    QApplication::processEvents();
    CHECK(!toggle->isChecked());
    CHECK(!advanced->isVisible());
    CHECK(toggle->isVisible());
}

TerrainModel makeGeoTerrain() {
    TerrainModel terrain;
    terrain.width = 101;
    terrain.height = 101;
    const double sampleDegrees = 0.0002;
    const double centerLatitude = 35.01;
    terrain.setCellSpacing(
        static_cast<float>(TerrainModel::metersPerDegreeLon(centerLatitude) * sampleDegrees),
        static_cast<float>(TerrainModel::metersPerDegreeLat(centerLatitude) * sampleDegrees));
    terrain.setGeoReference(35.0, -80.0, sampleDegrees, sampleDegrees);
    terrain.heights.assign(terrain.width * terrain.height, 125.0f);
    return terrain;
}

void testPreciseSourcePositionDialog() {
    const TerrainModel terrain = makeGeoTerrain();
    const glm::vec2 start = terrain.latLonToWorld(35.005, -79.995);
    Antenna antenna;
    antenna.position = {start.x, 140.0f, start.y};

    SourcePositionDialog dialog(terrain, antenna);
    auto* format = dialog.findChild<QComboBox*>("SourcePositionFormatCombo");
    auto* latitude = dialog.findChild<QDoubleSpinBox*>("SourceLatitudeSpin");
    auto* longitude = dialog.findChild<QDoubleSpinBox*>("SourceLongitudeSpin");
    auto* mgrs = dialog.findChild<QLineEdit*>("SourceMgrsEdit");
    auto* move = dialog.findChild<QPushButton*>("MoveSourceButton");
    CHECK(format && latitude && longitude && mgrs && move);
    if (!format || !latitude || !longitude || !mgrs || !move) return;
    CHECK(format->count() == 3);
    CHECK(!mgrs->text().isEmpty());

    format->setCurrentIndex(1);
    latitude->setValue(35.012);
    longitude->setValue(-79.988);
    move->click();
    QApplication::processEvents();
    CHECK(dialog.result() == QDialog::Accepted);
    const glm::vec2 expected = terrain.latLonToWorld(35.012, -79.988);
    CHECK(std::abs(dialog.worldX() - expected.x) < 0.1f);
    CHECK(std::abs(dialog.worldZ() - expected.y) < 0.1f);
}

void testPointAnalysisPanelShowsExplanation() {
    PointAnalysisPanel panel;
    PointAnalysisResult analysis;
    analysis.valid = true;
    analysis.inService = false;
    analysis.servingSourceIndex = 0;
    analysis.desiredDbm = -104.5f;
    analysis.jammerDbm = -98.0f;
    analysis.snrDb = 20.0f;
    analysis.sinrDb = -6.6f;
    analysis.fadeMarginDb = -4.5f;
    analysis.distanceM = 2400.0f;
    analysis.fresnelClearanceRatio = 0.42f;
    analysis.losStatus = TerrainModel::LOSStatus::Clear;
    analysis.explanations = {"SINR is below the requirement.",
                             "The Fresnel zone is obstructed."};
    analysis.profileDistanceM = {0.0f, 1200.0f, 2400.0f};
    analysis.terrainElevationM = {100.0f, 125.0f, 100.0f};
    analysis.lineOfSightElevationM = {130.0f, 125.0f, 120.0f};
    analysis.fresnel60LowerElevationM = {130.0f, 115.0f, 120.0f};
    panel.setAnalysis(analysis, "35.0°, -80.0°", "Emitter 1 · 450 MHz", "Jammer 1");

    auto* result = panel.findChild<QLabel*>("PointServiceResult");
    auto* serving = panel.findChild<QLabel*>("PointServingSource");
    auto* signal = panel.findChild<QLabel*>("PointSignalMetric");
    auto* explanation = panel.findChild<QLabel*>("PointExplanation");
    CHECK(result && serving && signal && explanation);
    if (!result || !serving || !signal || !explanation) return;
    CHECK(result->text() == "OUT OF SERVICE");
    CHECK(serving->text().contains("Emitter 1"));
    CHECK(signal->text().contains("-104.5"));
    CHECK(explanation->text().contains("SINR"));

    analysis.servingSourceIndex = -1;
    analysis.strongestJammerIndex = 0;
    analysis.explanations = {"No emitter reaches this result cell.",
                             "An in-band jammer is present."};
    analysis.profileDistanceM.clear();
    analysis.terrainElevationM.clear();
    analysis.lineOfSightElevationM.clear();
    analysis.fresnel60LowerElevationM.clear();
    panel.setAnalysis(analysis, "35.0°, -80.0°", {}, "Jammer 1 · 450 MHz");
    auto* path = panel.findChild<QLabel*>("PointPathSummary");
    CHECK(path && path->text().contains("Jammer 1"));
    panel.clear();
    CHECK(result->text() == "No result selected");
    CHECK(signal->text() == "—");
    CHECK(explanation->text().contains("Run coverage"));
}

void testScenarioComparisonPanelWorkflow() {
    ScenarioComparisonPanel panel;
    panel.resize(360, 620);
    panel.show();
    QApplication::processEvents();

    auto* status = panel.findChild<QLabel*>("ScenarioComparisonStatus");
    auto* baseline = panel.findChild<QLabel*>("ScenarioBaselineService");
    auto* current = panel.findChild<QLabel*>("ScenarioCurrentService");
    auto* net = panel.findChild<QLabel*>("ScenarioNetChange");
    auto* gained = panel.findChild<QLabel*>("ScenarioGainedCells");
    auto* setBaseline = panel.findChild<QPushButton*>("ScenarioSetBaselineButton");
    auto* replaceBaseline = panel.findChild<QPushButton*>("ScenarioReplaceBaselineButton");
    auto* clearBaseline = panel.findChild<QPushButton*>("ScenarioClearComparisonButton");
    auto* showChanges = panel.findChild<QCheckBox*>("ScenarioShowChangesCheck");
    CHECK(status && baseline && current && net && gained && setBaseline &&
          replaceBaseline && clearBaseline && showChanges);
    if (!status || !baseline || !current || !net || !gained || !setBaseline ||
        !replaceBaseline || !clearBaseline || !showChanges) return;

    CHECK(panel.state() == ScenarioComparisonPanel::State::NoBaseline);
    CHECK(!setBaseline->isEnabled());
    panel.showNoBaseline(true);
    CHECK(setBaseline->isEnabled());

    int baselineRequests = 0;
    QObject::connect(&panel, &ScenarioComparisonPanel::setBaselineRequested,
                     [&]() { ++baselineRequests; });
    setBaseline->click();
    CHECK(baselineRequests == 1);

    panel.showBaselineAwaitingCurrent(false);
    CHECK(panel.state() == ScenarioComparisonPanel::State::AwaitingCurrentResult);
    CHECK(!replaceBaseline->isEnabled());
    CHECK(clearBaseline->isEnabled());
    int clearRequests = 0;
    QObject::connect(&panel, &ScenarioComparisonPanel::clearComparisonRequested,
                     [&]() { ++clearRequests; });
    clearBaseline->click();
    CHECK(clearRequests == 1);

    ScenarioComparisonResult comparison;
    comparison.compatible = true;
    comparison.totalCells = 4;
    comparison.baselineServicePercent = 50.0;
    comparison.currentServicePercent = 75.0;
    comparison.gained = 1;
    comparison.lost = 0;
    comparison.retained = 2;
    comparison.unchanged = 1;
    comparison.meanDesiredDeltaDb = 2.5;
    comparison.meanSinrDeltaDb = -1.0;
    comparison.meanFadeMarginDeltaDb = 3.0;
    comparison.desiredDeltaSamples = 4;
    comparison.sinrDeltaSamples = 3;
    comparison.fadeMarginDeltaSamples = 4;
    comparison.serviceTransition = {0, 0, 1, 0};
    panel.setComparison(comparison);
    CHECK(panel.state() == ScenarioComparisonPanel::State::ComparisonReady);
    CHECK(status->text() == "COMPARISON READY");
    CHECK(baseline->text() == "50.0%");
    CHECK(current->text() == "75.0%");
    CHECK(net->text() == "+25.0 pp");
    CHECK(gained->text().startsWith("1"));
    CHECK(showChanges->isEnabled());
    panel.setChangesVisible(true);
    CHECK(panel.changesVisible());

    comparison.compatible = false;
    comparison.reason = "Different analysis quality.";
    panel.setComparison(comparison);
    CHECK(panel.state() == ScenarioComparisonPanel::State::Incompatible);
    CHECK(status->text() == "CANNOT COMPARE");
    CHECK(!showChanges->isEnabled());
    CHECK(!panel.changesVisible());
}

void testCalibrationPanelStatesAndSignals() {
    CalibrationPanel panel;
    panel.resize(360, 680);
    panel.show();
    QApplication::processEvents();

    auto* status = panel.findChild<QLabel*>("CalibrationStatus");
    auto* imported = panel.findChild<QLabel*>("CalibrationImportedCount");
    auto* usable = panel.findChild<QLabel*>("CalibrationUsableCount");
    auto* rejected = panel.findChild<QLabel*>("CalibrationRejectedCount");
    auto* meanResidual = panel.findChild<QLabel*>("CalibrationMeanResidual");
    auto* rmseBefore = panel.findChild<QLabel*>("CalibrationRmseBefore");
    auto* rmseAfter = panel.findChild<QLabel*>("CalibrationRmseAfter");
    auto* recommended = panel.findChild<QLabel*>("CalibrationRecommendedCorrection");
    auto* applied = panel.findChild<QLabel*>("CalibrationAppliedCorrection");
    auto* warnings = panel.findChild<QLabel*>("CalibrationWarnings");
    auto* importButton = panel.findChild<QPushButton*>("CalibrationImportButton");
    auto* applyButton = panel.findChild<QPushButton*>("CalibrationApplyCorrectionButton");
    auto* resetButton = panel.findChild<QPushButton*>("CalibrationResetCorrectionButton");
    auto* clearButton = panel.findChild<QPushButton*>("CalibrationClearButton");
    CHECK(status && imported && usable && rejected && meanResidual && rmseBefore &&
          rmseAfter && recommended && applied && warnings && importButton &&
          applyButton && resetButton && clearButton);
    if (!status || !imported || !usable || !rejected || !meanResidual ||
        !rmseBefore || !rmseAfter || !recommended || !applied || !warnings ||
        !importButton || !applyButton || !resetButton || !clearButton) return;

    int importRequests = 0;
    int applyRequests = 0;
    int resetRequests = 0;
    int clearRequests = 0;
    QObject::connect(&panel, &CalibrationPanel::importMeasurementsRequested,
                     [&]() { ++importRequests; });
    QObject::connect(&panel, &CalibrationPanel::applyRecommendedCorrectionRequested,
                     [&]() { ++applyRequests; });
    QObject::connect(&panel, &CalibrationPanel::resetAppliedCorrectionRequested,
                     [&]() { ++resetRequests; });
    QObject::connect(&panel, &CalibrationPanel::clearMeasurementsRequested,
                     [&]() { ++clearRequests; });

    CHECK(panel.state() == CalibrationPanel::State::NoMeasurements);
    CHECK(status->text() == "NO MEASUREMENTS");
    CHECK(!applyButton->isEnabled());
    CHECK(!resetButton->isEnabled());
    CHECK(!clearButton->isEnabled());
    importButton->click();
    CHECK(importRequests == 1);

    CalibrationPanel::Report report;
    report.importedSamples = 6;
    report.usableSamples = 0;
    report.rejectedSamples = 1;
    report.appliedCorrectionDb = 0.0;
    report.warnings = {QStringLiteral("Run coverage for this terrain.")};
    panel.showAwaitingCoverage(report);
    CHECK(panel.state() == CalibrationPanel::State::AwaitingCoverage);
    CHECK(status->text() == "COVERAGE REQUIRED");
    CHECK(imported->text() == "6");
    CHECK(usable->text() == "0");
    CHECK(rejected->text() == "1");
    CHECK(warnings->text().contains("Run coverage"));
    CHECK(!applyButton->isEnabled());
    CHECK(clearButton->isEnabled());

    report.usableSamples = 2;
    report.recommendedCorrectionDb = 3.25;
    panel.showInsufficientSamples(report);
    CHECK(panel.state() == CalibrationPanel::State::InsufficientSamples);
    CHECK(status->text() == "MORE MATCHES NEEDED");
    CHECK(!applyButton->isEnabled());

    report.usableSamples = 5;
    report.meanResidualDb = 3.25;
    report.medianResidualDb = 3.0;
    report.meanAbsoluteErrorDb = 4.5;
    report.meanAbsoluteErrorAfterDb = 1.2;
    report.rmseBeforeDb = 5.1;
    report.rmseAfterDb = 1.5;
    panel.setCalibrationReady(report);
    CHECK(panel.state() == CalibrationPanel::State::Ready);
    CHECK(status->text() == "CALIBRATION READY");
    CHECK(applyButton->isEnabled());
    CHECK(meanResidual->text().contains("3.25"));
    CHECK(rmseBefore->text().contains("5.10"));
    CHECK(rmseAfter->text().contains("1.50"));
    CHECK(recommended->text().contains("3.25"));
    applyButton->click();
    CHECK(applyRequests == 1);

    panel.setAppliedCorrection(3.25);
    CHECK(resetButton->isEnabled());
    CHECK(applied->text().contains("3.25"));
    resetButton->click();
    clearButton->click();
    CHECK(resetRequests == 1);
    CHECK(clearRequests == 1);

    CalibrationReport calibration;
    calibration.imported = 4;
    calibration.paired = 2;
    calibration.rejected = 2;
    calibration.meanBiasDb = 1.75;
    calibration.warnings = {"Two pairs are not enough."};
    panel.setCalibrationReport(calibration, -0.5);
    CHECK(panel.state() == CalibrationPanel::State::InsufficientSamples);
    CHECK(panel.report().importedSamples == 4);
    CHECK(panel.report().usableSamples == 2);
    CHECK(panel.report().rejectedSamples == 2);
    CHECK(panel.report().warnings.contains("Two pairs are not enough."));
    CHECK(!applyButton->isEnabled());
    CHECK(resetButton->isEnabled());

    calibration.paired = 3;
    panel.setCalibrationReport(calibration, 0.0);
    CHECK(panel.state() == CalibrationPanel::State::Ready);
    CHECK(applyButton->isEnabled());
    CHECK(!resetButton->isEnabled());

    panel.showNoMeasurements();
    CHECK(panel.state() == CalibrationPanel::State::NoMeasurements);
    CHECK(panel.report().appliedCorrectionDb == 0.0);
    CHECK(!clearButton->isEnabled());
}

void testCsvUsesClampedEdgeCellCenter() {
    FieldGrid grid;
    grid.width = 2;
    grid.height = 1;
    grid.cellSize = 60.0f;
    grid.originXZ = {30.0f, 30.0f};
    grid.maxXZ = {80.0f, 30.0f};
    grid.dbm = {-70.0f, -80.0f};
    const std::string path = "test_edge_cell_export.csv";
    CHECK(Exporter::exportCsv(grid, path));
    std::ifstream input(path);
    std::string header;
    std::string first;
    std::string second;
    std::getline(input, header);
    std::getline(input, first);
    std::getline(input, second);
    input.close();
    std::remove(path.c_str());
    CHECK(first.rfind("30.000,30.000,", 0) == 0);
    CHECK(second.rfind("80.000,30.000,", 0) == 0);
}

void testMgrsDialogInput() {
    LocationDialog dialog(0.0, 0.0, 10.0);
    auto* format = dialog.findChild<QComboBox*>("CoordinateFormatCombo");
    auto* mgrs = dialog.findChild<QLineEdit*>("MgrsEdit");
    auto* apply = dialog.findChild<QPushButton*>("ApplyMgrsButton");
    CHECK(format && mgrs && apply);
    if (!format || !mgrs || !apply) return;

    format->setCurrentIndex(1);
    mgrs->setText("18S UJ 23394 07395");
    apply->click();
    QApplication::processEvents();
    CHECK(std::abs(dialog.latitude() - 38.8977) < 0.0002);
    CHECK(std::abs(dialog.longitude() + 77.0365) < 0.0002);
}

void testDebouncedCustomGeocoderSuggestions() {
    QTcpServer server;
    CHECK(server.listen(QHostAddress::LocalHost, 0));
    if (!server.isListening()) return;

    QByteArray requestBytes;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            requestBytes += socket->readAll();
            if (!requestBytes.contains("\r\n\r\n")) return;
            const QByteArray body =
                R"([{"display_name":"Raleigh, North Carolina, USA","lat":"35.7796","lon":"-78.6382"}])";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });

    const QByteArray endpoint = QByteArray("http://127.0.0.1:")
        + QByteArray::number(server.serverPort()) + "/search";
    qputenv("RIDGELINE_GEOCODER_URL", endpoint);
    LocationDialog dialog(0.0, 0.0, 10.0);
    auto* edit = dialog.findChild<QLineEdit*>("LocationSearchEdit");
    auto* results = dialog.findChild<QListWidget*>("SearchResults");
    CHECK(edit && results);
    if (edit && results) {
        edit->setText("Ral");
        QMetaObject::invokeMethod(&dialog, "onSearchTextEdited",
                                  Q_ARG(QString, QStringLiteral("Ral")));
        CHECK(waitUntil([&]() { return results->count() == 1; }, 2500));
        CHECK(requestBytes.contains("q=Ral"));
        CHECK(results->item(0)->text().contains("Raleigh"));

        // Repeating the exact typeahead query must be served from QSettings,
        // not sent to the configured endpoint a second time.
        QMetaObject::invokeMethod(&dialog, "onSearchTextEdited",
                                  Q_ARG(QString, QStringLiteral("Ral")));
        waitUntil([]() { return false; }, 700);
        CHECK(requestBytes.count("GET ") == 1);
    }
    qunsetenv("RIDGELINE_GEOCODER_URL");
}

void testRuntimeAssetsIgnoreLaunchDirectory() {
    QTemporaryDir temp;
    CHECK(temp.isValid());
    if (!temp.isValid()) return;

    const QString applicationDir = QDir(temp.path()).filePath("packaged");
    const QString workingDir = QDir(temp.path()).filePath("unrelated-launch-dir");
    const QString relative = "assets/shaders/basemap.vert";
    const QString packagedFile = QDir(applicationDir).filePath(relative);
    const QString workingFile = QDir(workingDir).filePath(relative);
    CHECK(QDir().mkpath(QFileInfo(packagedFile).absolutePath()));
    CHECK(QDir().mkpath(QFileInfo(workingFile).absolutePath()));

    QFile packaged(packagedFile);
    CHECK(packaged.open(QIODevice::WriteOnly));
    packaged.write("packaged shader");
    packaged.close();
    QFile working(workingFile);
    CHECK(working.open(QIODevice::WriteOnly));
    working.write("wrong launch-directory shader");
    working.close();

    CHECK(RuntimePaths::resolveFromRoots(
              relative, applicationDir, workingDir) ==
          QDir::cleanPath(packagedFile));

    const QString missing = "assets/shaders/missing.vert";
    CHECK(RuntimePaths::resolveFromRoots(
              missing, applicationDir, workingDir) ==
          QDir::cleanPath(QDir(applicationDir).filePath(missing)));
}

} // namespace

int main(int argc, char** argv) {
#ifdef Q_OS_WIN
    qputenv("QT_QPA_PLATFORM", "windows");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("RidgelineTests");
    QCoreApplication::setApplicationName("RidgelineUiTests");

    testAdvancedPanelRemainsEditableAndCollapsible();
    testPreciseSourcePositionDialog();
    testPointAnalysisPanelShowsExplanation();
    testScenarioComparisonPanelWorkflow();
    testCalibrationPanelStatesAndSignals();
    testCsvUsesClampedEdgeCellCenter();
    testMgrsDialogInput();
    testDebouncedCustomGeocoderSuggestions();
    testRuntimeAssetsIgnoreLaunchDirectory();

    if (failures == 0)
        std::cout << "UI interaction tests passed.\n";
    return failures == 0 ? 0 : 1;
}
