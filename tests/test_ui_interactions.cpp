#include "data/AntennaLibrary.h"
#include "ui/LocationDialog.h"
#include "ui/PropertiesPanel.h"
#include "ui/SourcePositionDialog.h"
#include "ui/PointAnalysisPanel.h"
#include "ui/ScenarioComparisonPanel.h"
#include "ui/CalibrationPanel.h"
#include "ui/OverlayToolbar.h"
#include "ui/LayersPanel.h"
#include "ui/VegetationAreaDialog.h"
#include "map/LayerStack.h"
#include "ui/ScenarioTree.h"
#include "ui/WorkspaceTheme.h"
#include "ui/TerrainPresentation.h"
#include <QMenu>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include "sim/TerrainModel.h"
#include "sim/ScenarioComparison.h"
#include "sim/MeasurementCalibration.h"
#include "data/Exporter.h"
#include "data/RasterTiff.h"
#include "data/CoverageGeoTiff.h"
#include "RuntimePaths.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHostAddress>
#include <QImage>
#include <QLineEdit>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QPixmap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <functional>
#include <fstream>
#include <sstream>
#include <locale>
#include <iostream>
#include <limits>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAIL: " #condition " (line " << __LINE__ << ")\n"; \
        ++failures; \
    } \
} while (0)

// Not NEAR: <windows.h> defines that as an empty macro.
#define CHECK_NEAR(a, b, tolerance) \
    CHECK(std::abs(static_cast<double>(a) - static_cast<double>(b)) <= (tolerance))

// Optional offscreen QA artifacts, using only synthetic test data. A caller
// supplies a new directory; never overwrite an existing capture.
void capturePanel(QWidget& panel, const QString& name) {
    const QString directory = qEnvironmentVariable("RIDGELINE_QA_CAPTURE_DIR");
    if (directory.isEmpty()) return;
    CHECK(QDir().mkpath(directory));
    const QString path = QDir(directory).filePath(name + ".png");
    CHECK(!QFileInfo::exists(path));
    if (!QFileInfo::exists(path)) CHECK(panel.grab().save(path, "PNG"));
}

bool waitUntil(const std::function<bool()>& condition, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (condition()) return true;
    }
    return condition();
}

void testCompactMapControls() {
    auto area = [](double x, double z) {
        return TerrainPresentation::areaSummary(x, z).replace(QChar(0x00a0), ' ');
    };
    CHECK(area(22, 31) == "22.0 × 31.0 m");
    CHECK(area(15000, 15000) == "15.0 × 15.0 km");
    CHECK(area(999.9, 1000) == "999.9 × 1000.0 m");
    CHECK(area(1000, 1000) == "1.0 × 1.0 km");
    CHECK(area(15000, 31) == "15000.0 × 31.0 m");
    CHECK(area(0.01, 0.09) == "<0.1 × <0.1 m");
    CHECK(area(0, 31) == "Area unavailable");
    CHECK(area(-1, 31) == "Area unavailable");
    CHECK(area(std::numeric_limits<double>::infinity(), 31) == "Area unavailable");
    CHECK(area(31, std::numeric_limits<double>::quiet_NaN()) == "Area unavailable");
    TerrainModel rectangular;
    rectangular.width = 120;
    rectangular.height = 80;
    rectangular.setCellSpacing(12.5f, 30.0f);
    const QString details = TerrainPresentation::gridDetails(rectangular);
    CHECK(details.contains("120 × 80 samples"));
    CHECK(details.contains("12.5 m east–west × 30.0 m north–south"));
    // The old blanket disclaimer is replaced by a concrete statement: this
    // model carries no source resolution, so it must say so rather than let
    // the grid spacing be read as the data's resolution.
    CHECK(details.contains("Source resolution is not known"));
    rectangular.sourceResolutionM = 30.0f;
    const QString withSource = TerrainPresentation::gridDetails(rectangular);
    CHECK(withSource.contains("Source resolution: about 30.0 m"));
    CHECK(withSource.contains("interpolated"));
    for (auto type : {TerrainSource::Type::Hills, TerrainSource::Type::Flat}) {
        CHECK(TerrainPresentation::elevationReference(type).contains("Synthetic"));
        CHECK(!TerrainPresentation::elevationReference(type).contains("MSL"));
    }
    for (auto type : {TerrainSource::Type::Hgt, TerrainSource::Type::Dted,
                      TerrainSource::Type::GeoTiff, TerrainSource::Type::Global}) {
        CHECK(TerrainPresentation::elevationReference(type).contains("datum unverified"));
        CHECK(!TerrainPresentation::elevationReference(type).contains("MSL"));
    }
    CHECK(TerrainPresentation::elevationCredit(TerrainSource::Type::Global).contains("Provider unverified"));
    CHECK(!TerrainPresentation::elevationCredit(TerrainSource::Type::Global).contains("Mapzen"));
    CHECK(TerrainPresentation::elevationCredit(TerrainSource::Type::Global,
        TerrainModel::ElevationProvider::Mapzen).contains("Mapzen"));
    const QString customCredit = TerrainPresentation::elevationCredit(TerrainSource::Type::Global,
        TerrainModel::ElevationProvider::Custom);
    CHECK(customCredit.contains("custom Terrarium") && !customCredit.contains("Mapzen"));
    CHECK(TerrainPresentation::elevationCredit(TerrainSource::Type::Hills).contains("Demo only"));
    CHECK(!TerrainPresentation::elevationCredit(TerrainSource::Type::Hgt).contains("Mapzen"));
    CHECK(TerrainPresentation::elevationCredit(TerrainSource::Type::GeoTiff).contains("GeoTIFF"));
    CHECK(TerrainPresentation::elevationCredit(TerrainSource::Type::Dted).contains("DTED"));
    CHECK(TerrainPresentation::elevationCredit(TerrainSource::Type::Flat).contains("Synthetic"));
    OverlayToolbar toolbar;
    toolbar.setStyleSheet(WorkspaceTheme::styleSheet() + toolbar.styleSheet());
    toolbar.resize(900, 50);
    toolbar.show();
    QApplication::processEvents();
    auto* metric = toolbar.findChild<QComboBox*>("CoverageMetricCombo");
    auto* reset = toolbar.findChild<QToolButton*>("ResetMapViewButton");
    auto* top = toolbar.findChild<QToolButton*>("TopMapViewButton");
    CHECK(metric && reset && top);
    if (!metric || !reset || !top) return;
    CHECK(metric->count() == 7);
    CHECK(!metric->accessibleName().isEmpty());
    HeatmapOverlay::Metric selected = HeatmapOverlay::Metric::DesiredPower;
    QObject::connect(&toolbar, &OverlayToolbar::heatmapMetricChanged,
                     [&](HeatmapOverlay::Metric value) { selected = value; });
    for (int i = 0; i < metric->count(); ++i) {
        metric->setCurrentIndex(i);
        CHECK(static_cast<int>(selected) == metric->itemData(i).toInt());
    }
    // Layer visibility, opacity and order moved to the Layers dock, which owns
    // them alone so the two controls cannot disagree. The dock is covered by
    // testLayersPanelDrivesTheStack below.
    capturePanel(toolbar, "map-controls-toolbar");
    int resets = 0;
    QObject::connect(&toolbar, &OverlayToolbar::resetViewRequested, [&]() { ++resets; });
    reset->click();
    CHECK(resets == 1);
    CHECK(toolbar.rect().contains(reset->geometry()));
    CHECK(toolbar.sizeHint().width() <= 900);
    int topViews = 0;
    QObject::connect(&toolbar, &OverlayToolbar::topViewRequested, [&]() { ++topViews; });
    top->click();
    CHECK(topViews == 1);
}

void testSourceCardsPreserveSelection() {
    ScenarioTree tree;
    tree.setStyleSheet(WorkspaceTheme::styleSheet());
    tree.resize(350, 260);
    tree.show();
    Antenna emitter;
    Antenna jammer;
    jammer.isJammer = true;
    tree.refresh({emitter, jammer});
    tree.selectRow(1);
    auto* list = tree.findChild<QListWidget*>("SourceList");
    CHECK(list && list->count() == 2);
    if (!list) return;
    CHECK(list->item(0)->text().startsWith("Emitter 1\n"));
    CHECK(list->item(1)->text().startsWith("Jammer 1\n"));
    CHECK(!list->item(1)->toolTip().isEmpty());
    emitter.frequencyMHz = 433.0f;
    tree.refresh({emitter, jammer});
    CHECK(tree.selectedIndex() == 1);
    CHECK(list->item(0)->text().contains("433 MHz"));
    tree.refresh({emitter});
    CHECK(tree.selectedIndex() == -1);
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
    const auto spins = panel.findChildren<QDoubleSpinBox*>();
    CHECK(!spins.isEmpty());
    for (auto* spin : spins) {
        CHECK(!spin->accessibleName().isEmpty());
        CHECK(spin->buttonSymbols() == QAbstractSpinBox::UpDownArrows);
        const double before = spin->value();
        if (before + spin->singleStep() <= spin->maximum()) {
            spin->stepUp();
            CHECK(spin->value() > before);
            spin->stepDown();
            CHECK(std::abs(spin->value() - before) < 0.001);
            if (spin->isVisible()) {
                const auto key = [spin](int code) {
                    QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
                    QApplication::sendEvent(spin, &press);
                    QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
                    QApplication::sendEvent(spin, &release);
                };
                key(Qt::Key_Up);
                CHECK(spin->value() > before);
                key(Qt::Key_Down);
                CHECK(std::abs(spin->value() - before) < 0.001);
            }
        }
    }
    capturePanel(panel, "source-settings-spinboxes");
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
    auto* status = dialog.findChild<QLabel*>("PositionStatus");
    CHECK(status && status->text().contains("native vertical reference"));
    CHECK(status && !status->text().contains("MSL"));
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

    panel.resize(310, 480);
    panel.show();
    QApplication::processEvents();
    CHECK(panel.width() == 310 && panel.height() == 480);
    auto* scroll = panel.findChild<QScrollArea*>("ResultInspectorScrollArea");
    CHECK(scroll && scroll->verticalScrollBar()->maximum() > 0);
    CHECK(scroll && scroll->widget()->width() <= scroll->viewport()->width());
    CHECK(signal->font().pixelSize() >= 15);
    CHECK(explanation->font().pixelSize() >= 13);
    for (auto* label : panel.findChildren<QLabel*>()) {
        CHECK(label->font().pixelSize() >= 12);
        if (scroll) CHECK(label->width() <= scroll->viewport()->width());
    }
    if (scroll) {
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        const QRect bounds(explanation->mapTo(scroll->viewport(), QPoint{}), explanation->size());
        CHECK(scroll->viewport()->rect().contains(bounds));
    }

    // Foliage is named only when the path pays for it, and the canopy is drawn
    // on the profile above the terrain.
    auto* pathSummary = panel.findChild<QLabel*>("PointPathSummary");
    auto* plot = panel.findChild<QWidget*>("LinkProfilePlot");
    CHECK(pathSummary && plot);
    if (!pathSummary || !plot) return;
    CHECK(!pathSummary->text().contains("Foliage"));
    CHECK(pathSummary->text().contains("Model: No serving path"));
    analysis.pathModel = PathModel::Itm;
    panel.setAnalysis(analysis, "35.0°, -80.0°", "Emitter 1 · 450 MHz", "Jammer 1");
    CHECK(pathSummary->text().contains("Model: NTIA ITM"));
    CHECK(pathSummary->text().contains("Jammer 1"));
    auto canopyPixels = [plot] {
        const QImage image = plot->grab().toImage();
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.green() > c.red() + 25 && c.green() > c.blue() + 50) ++count;
            }
        return count;
    };
    CHECK(canopyPixels() == 0);
    analysis.foliageDepthM = 180.0f;
    analysis.foliageLossDb = 9.4f;
    analysis.canopyTopElevationM = {100.0f, 145.0f, 100.0f};
    panel.setAnalysis(analysis, "35.0°, -80.0°", "Emitter 1 · 450 MHz", "Jammer 1");
    CHECK(pathSummary->text().contains("Foliage: 180 m, 9.4 dB"));
    CHECK(canopyPixels() > 50);
    analysis.foliageDepthM = 0.0f;
    analysis.foliageLossDb = 0.0f;
    analysis.canopyTopElevationM.clear();

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

    // A compact comparison dock must grow vertically, never clip sideways.
    comparison.compatible = true;
    panel.setComparison(comparison);
    panel.resize(310, 480);
    QApplication::processEvents();
    auto* scroll = panel.findChild<QScrollArea*>("ScenarioComparisonScrollArea");
    CHECK(scroll && scroll->verticalScrollBar()->maximum() > 0);
    CHECK(panel.width() == 310 && panel.height() == 480);
    CHECK(scroll && scroll->widget()->width() <= scroll->viewport()->width());
    for (auto* label : panel.findChildren<QLabel*>()) {
        CHECK(label->font().pixelSize() >= 12);
        if (scroll) CHECK(label->width() <= scroll->viewport()->width());
    }
    CHECK(net->font().pixelSize() >= 14);
    CHECK(replaceBaseline->font().pixelSize() >= 13);
    if (scroll) scroll->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    capturePanel(panel, "comparison-compact-top");
    if (scroll) {
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        const QRect bounds(clearBaseline->mapTo(scroll->viewport(), QPoint{}), clearBaseline->size());
        CHECK(scroll->viewport()->rect().contains(bounds));
        capturePanel(panel, "comparison-compact-bottom");
    }
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

    // Long warnings and large counts must stay readable in a narrow dock.
    report.importedSamples = 1234567;
    report.usableSamples = 1234000;
    report.rejectedSamples = 567;
    report.warnings = {QStringLiteral("Some measurements fall outside the analysis area. Review the input coordinates before applying a correction.")};
    panel.setCalibrationReady(report);
    panel.resize(320, 480);
    QApplication::processEvents();
    auto* scroll = panel.findChild<QScrollArea*>("CalibrationScrollArea");
    CHECK(panel.width() == 320 && panel.height() == 480);
    CHECK(scroll && scroll->verticalScrollBar()->maximum() > 0);
    CHECK(scroll && scroll->widget()->width() <= scroll->viewport()->width());
    for (auto* label : panel.findChildren<QLabel*>()) {
        CHECK(label->font().pixelSize() >= 12);
        if (scroll) CHECK(label->width() <= scroll->viewport()->width());
    }
    CHECK(recommended->font().pixelSize() >= 14);
    CHECK(applyButton->font().pixelSize() >= 13);
    CHECK(warnings->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
    if (scroll) {
        scroll->verticalScrollBar()->setValue(0);
        QApplication::processEvents();
        capturePanel(panel, "calibration-compact-top");
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        const QRect bounds(clearButton->mapTo(scroll->viewport(), QPoint{}), clearButton->size());
        CHECK(scroll->viewport()->rect().contains(bounds));
        capturePanel(panel, "calibration-compact-bottom");
    }
}

// A 3 x 2 result grid of 200 m cells on terrain whose samples are 100 m and
// 0.001 degrees apart, so every expected coordinate is a round number.
void geoTiffFixture(FieldGrid& grid, TerrainModel& terrain) {
    terrain.width = 7;
    terrain.height = 5;
    terrain.setCellSpacing(100.0f, 100.0f);
    terrain.heights.assign(35, 0.0f);
    terrain.setGeoReference(35.0, -80.0, 0.001, 0.001);
    grid.width = 3;
    grid.height = 2;
    grid.cellSize = 200.0f;
    grid.originXZ = {100.0f, 100.0f};
    grid.maxXZ = {600.0f, 400.0f};
    const float none = -std::numeric_limits<float>::infinity();
    grid.dbm = {-70.0f, -75.0f, -80.0f,   // south row
                -85.0f, none, -90.0f};    // north row
}

void testGeoTiffExportPlacesEachCell() {
    FieldGrid grid;
    TerrainModel terrain;
    geoTiffFixture(grid, terrain);
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("地形-coverage.tif"));
    CHECK(Exporter::exportGeoTiff(grid, terrain, path.toStdString()));

    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();
    RasterTiff raster;
    try {
        raster = RasterTiff::decode(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    } catch (const std::exception& e) {
        std::cerr << "GeoTIFF did not decode: " << e.what() << '\n';
        CHECK(false);
        return;
    }
    CHECK(raster.width == 3 && raster.height == 2);
    CHECK(raster.bitsPerSample == 32 && raster.sampleFormat == RasterTiff::Sample::Float);
    CHECK(raster.georeferenced);
    CHECK_NEAR(raster.westLonDeg, -80.0, 1e-9);
    CHECK_NEAR(raster.northLatDeg, 35.004, 1e-9);
    CHECK_NEAR(raster.lonStepDeg, 0.002, 1e-12);
    CHECK_NEAR(raster.latStepDeg, 0.002, 1e-12);

    // North row first, and the empty cell is NaN rather than a number.
    CHECK_NEAR(raster.at(0, 0), -85.0, 1e-6);
    CHECK(std::isnan(raster.at(1, 0)));
    CHECK_NEAR(raster.at(2, 0), -90.0, 1e-6);
    CHECK_NEAR(raster.at(0, 1), -70.0, 1e-6);
    CHECK_NEAR(raster.at(1, 1), -75.0, 1e-6);
    CHECK_NEAR(raster.at(2, 1), -80.0, 1e-6);

    // Each pixel's centre is where the solver sampled its cell.
    for (int gz = 0; gz < grid.height; ++gz) {
        for (int gx = 0; gx < grid.width; ++gx) {
            const glm::vec2 centre = grid.cellCenter(gx, gz);
            const auto expected = terrain.worldToLatLon(centre.x, centre.y);
            const int row = grid.height - 1 - gz;
            CHECK_NEAR(raster.westLonDeg + (gx + 0.5) * raster.lonStepDeg, expected.lonDeg, 1e-9);
            CHECK_NEAR(raster.northLatDeg - (row + 0.5) * raster.latStepDeg, expected.latDeg, 1e-9);
        }
    }

    // Terrain with no real-world location cannot be placed: refuse, and leave
    // an existing file alone.
    TerrainModel local = terrain;
    local.georeferenced = false;
    CHECK(!CoverageGeoTiff::canEncode(grid, local));
    CHECK(!Exporter::exportGeoTiff(grid, local, path.toStdString()));
    CHECK(!Exporter::exportGeoTiff(FieldGrid{}, terrain, path.toStdString()));
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == bytes);
    file.close();
}

void testCsvUsesClampedEdgeCellCenter() {
    FieldGrid grid;
    grid.width = 2;
    grid.height = 1;
    grid.cellSize = 60.0f;
    grid.originXZ = {30.0f, 30.0f};
    grid.maxXZ = {80.0f, 30.0f};
    grid.dbm = {-70.0f, -80.0f};
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("\u5730\u5f62-\u00e9.csv"));
    struct CommaDecimal : std::numpunct<char> {
        char do_decimal_point() const override { return ','; }
    };
    {
        struct RestoreLocale {
            std::locale previous = std::locale();
            ~RestoreLocale() { std::locale::global(previous); }
        } restore;
        std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
        CHECK(Exporter::exportCsv(grid, path.toStdString()));
    }
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray original = file.readAll();
    file.close();
    std::istringstream input(original.toStdString());
    std::string header;
    std::string first;
    std::string second;
    std::getline(input, header);
    std::getline(input, first);
    std::getline(input, second);
    CHECK(first.rfind("30.000,30.000,", 0) == 0);
    CHECK(second.rfind("80.000,30.000,", 0) == 0);
    CHECK(!Exporter::exportCsv(FieldGrid{}, path.toStdString()));
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == original);
    file.close();
    CHECK(!Exporter::exportCsv(grid, directory.path().toStdString()));
    grid.dbm[0] = -65.0f;
#ifdef Q_OS_WIN
    {
        const auto nativePath = path.toStdWString();
        struct LockedFile {
            HANDLE handle;
            ~LockedFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        } locked{CreateFileW(nativePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        CHECK(locked.handle != INVALID_HANDLE_VALUE);
        if (locked.handle != INVALID_HANDLE_VALUE) {
            CHECK(!Exporter::exportCsv(grid, path.toStdString()));
            CHECK(file.open(QIODevice::ReadOnly));
            CHECK(file.readAll() == original);
            file.close();
            CHECK(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).size() == 1);
        }
    }
#endif
    CHECK(Exporter::exportCsv(grid, path.toStdString()));
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll().contains("-65.000"));
    file.close();
    CHECK(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).size() == 1);
    const QString pngPath = directory.filePath(QStringLiteral("\u5730\u5f62-\u00e9.png"));
    QImage originalImage(3, 2, QImage::Format_ARGB32);
    originalImage.fill(QColor(10, 20, 30, 180));
    CHECK(Exporter::savePng(originalImage, pngPath.toStdString()));
    CHECK(QImage(pngPath).pixelColor(1, 1) == QColor(10, 20, 30, 180));
    CHECK(QImage(pngPath).size() == originalImage.size());
    CHECK(!Exporter::savePng(QImage{}, pngPath.toStdString()));
    CHECK(QImage(pngPath).pixelColor(1, 1) == QColor(10, 20, 30, 180));
    CHECK(!Exporter::savePng(originalImage, directory.path().toStdString()));
    originalImage.fill(Qt::green);
#ifdef Q_OS_WIN
    {
        QFile png(pngPath);
        CHECK(png.open(QIODevice::ReadOnly));
        const QByteArray before = png.readAll();
        png.close();
        const auto nativePath = pngPath.toStdWString();
        struct PngLock {
            HANDLE handle;
            ~PngLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        } lock{CreateFileW(nativePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        CHECK(lock.handle != INVALID_HANDLE_VALUE);
        if (lock.handle != INVALID_HANDLE_VALUE) {
            CHECK(!Exporter::savePng(originalImage, pngPath.toStdString()));
            CHECK(png.open(QIODevice::ReadOnly));
            CHECK(png.readAll() == before);
            png.close();
            CHECK(QImage(pngPath).pixelColor(1, 1) == QColor(10, 20, 30, 180));
            CHECK(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).size() == 2);
        }
    }
#endif
    CHECK(Exporter::savePng(originalImage, pngPath.toStdString()));
    CHECK(QImage(pngPath).pixelColor(1, 1) == QColor(Qt::green));
    grid.height = 2;
    grid.maxXZ = {80.0f, 80.0f};
    grid.dbm = {-80.0f, -60.0f, -70.0f, std::numeric_limits<float>::quiet_NaN()};
    CHECK(Exporter::exportCsv(grid, path.toStdString()));
    CHECK(file.open(QIODevice::ReadOnly));
    const auto rows = file.readAll().split('\n');
    file.close();
    CHECK(rows.size() == 6); // Header, four cells, final empty line.
    if (rows.size() == 6) {
        CHECK(rows[3].startsWith("30.000,80.000,-70.000,"));
        CHECK(rows[4].startsWith("80.000,80.000,nan,"));
        for (int i = 1; i <= 4; ++i) CHECK(rows[i].split(',').size() == 9);
    }
    CHECK(Exporter::exportPng(grid, pngPath.toStdString()));
    const QImage heatmap(pngPath);
    CHECK(heatmap.size() == QSize(2, 2));
    CHECK(heatmap.pixelColor(0, 0) == QColor(Qt::red));
    CHECK(heatmap.pixelColor(1, 0) == QColor(Qt::green));
    CHECK(heatmap.pixelColor(0, 1) == QColor(Qt::yellow));
    CHECK(heatmap.pixelColor(1, 1).alpha() == 0);
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

void testVegetationAreaDialog() {
    const VegetationArea stand{10.0f, 20.0f, 110.0f, 90.0f, 18.0f, "north wood"};
    VegetationAreaDialog dialog(stand, "solves as 110 × 80 m (11 × 8 samples)", 12.0f);
    auto* name = dialog.findChild<QLineEdit*>("VegetationAreaName");
    auto* kind = dialog.findChild<QComboBox*>("VegetationAreaKind");
    auto* height = dialog.findChild<QDoubleSpinBox*>("VegetationAreaHeight");
    auto* effect = dialog.findChild<QLabel*>("VegetationAreaEffect");
    CHECK(dialog.objectName() == "VegetationAreaDialog");
    CHECK(name && kind && height && effect);
    if (!name || !kind || !height || !effect) return;
    CHECK(std::abs(height->maximum() - VegetationAreas::kMaxCanopyHeightM) < 1e-6);
    CHECK(std::abs(height->minimum() - 0.5) < 1e-6);
    CHECK(std::abs(height->value() - 18.0) < 1e-6);
    CHECK(effect->text().contains("solves as"));

    kind->setCurrentIndex(kind->findData(static_cast<int>(VegetationKind::Clearing)));
    CHECK(!height->isEnabled());
    const VegetationArea cleared = dialog.area();
    CHECK(cleared.kind == VegetationKind::Clearing);
    CHECK(cleared.canopyHeightM == 0.0f);
    CHECK(cleared.minX == stand.minX && cleared.maxZ == stand.maxZ);

    kind->setCurrentIndex(kind->findData(static_cast<int>(VegetationKind::Stand)));
    CHECK(height->isEnabled() && std::abs(height->value() - 18.0) < 1e-6);

    height->setValue(22.5);
    name->setText("  south wood  ");
    const VegetationArea edited = dialog.area();
    CHECK(edited.name == "south wood");
    CHECK(std::abs(edited.canopyHeightM - 22.5f) < 1e-4f);
    name->setText("   ");
    CHECK(dialog.area().name == "north wood");

    // A clearing turned into a stand starts from the session's stand height,
    // not from the zero a clearing carries.
    const VegetationArea clearing{0.0f, 0.0f, 50.0f, 50.0f, 0.0f, "cut", VegetationKind::Clearing};
    VegetationAreaDialog clearingDialog(clearing, "", 12.0f);
    auto* clearingKind = clearingDialog.findChild<QComboBox*>("VegetationAreaKind");
    auto* clearingHeight = clearingDialog.findChild<QDoubleSpinBox*>("VegetationAreaHeight");
    CHECK(clearingKind && clearingHeight && !clearingHeight->isEnabled());
    if (clearingKind && clearingHeight) {
        clearingKind->setCurrentIndex(
            clearingKind->findData(static_cast<int>(VegetationKind::Stand)));
        CHECK(clearingHeight->isEnabled() && std::abs(clearingHeight->value() - 12.0) < 1e-6);
    }
    // An untouched dialog changes nothing, even for a stored height outside the
    // spin box's range: OK must not rewrite it, rebuild the canopy or add an
    // undo step. A real edit still wins.
    for (const float stored : {0.3f, 150.0f}) {
        const VegetationArea odd{0.0f, 0.0f, 50.0f, 50.0f, stored, "odd"};
        VegetationAreaDialog oddDialog(odd, "", 12.0f);
        auto* oddName = oddDialog.findChild<QLineEdit*>("VegetationAreaName");
        auto* oddKind = oddDialog.findChild<QComboBox*>("VegetationAreaKind");
        auto* oddHeight = oddDialog.findChild<QDoubleSpinBox*>("VegetationAreaHeight");
        CHECK(oddName && oddKind && oddHeight);
        if (!oddName || !oddKind || !oddHeight) continue;
        CHECK(oddDialog.area().canopyHeightM == stored);
        oddName->setText("renamed");
        CHECK(oddDialog.area().canopyHeightM == stored);
        oddKind->setCurrentIndex(oddKind->findData(static_cast<int>(VegetationKind::Clearing)));
        oddKind->setCurrentIndex(oddKind->findData(static_cast<int>(VegetationKind::Stand)));
        CHECK(oddDialog.area().canopyHeightM == stored);
        oddHeight->setValue(40.0);
        CHECK(std::abs(oddDialog.area().canopyHeightM - 40.0f) < 1e-4f);
    }
    capturePanel(dialog, "vegetation-area-dialog");
}

void testLayersPanelDrivesTheStack() {
    LayerStack stack;
    LayersPanel panel;
    panel.setStyleSheet(WorkspaceTheme::styleSheet());
    panel.setStack(&stack);
    panel.resize(320, 420);
    panel.show();
    QApplication::processEvents();

    auto* list = panel.findChild<QListWidget*>("LayerList");
    CHECK(list && list->count() == static_cast<int>(stack.size()));

    // Rows read top-down while the stack is bottom-first, so the topmost row
    // must be the last layer. Getting this backwards would silently invert
    // which layer draws over which.
    CHECK(list && list->item(0)->data(Qt::UserRole).toString() == "fresnel");
    CHECK(list && list->item(list->count() - 1)->data(Qt::UserRole).toString() == "satellite");

    auto* hillshade = panel.findChild<QCheckBox*>("LayerVisiblehillshade");
    CHECK(hillshade && !hillshade->isChecked());
    if (hillshade) {
        hillshade->setChecked(true);
        QApplication::processEvents();
        CHECK(stack.find("hillshade")->visible);
    }

    auto* opacity = panel.findChild<QSlider*>("LayerOpacitysatellite");
    CHECK(opacity && opacity->value() == 86);
    if (opacity) {
        opacity->setValue(30);
        QApplication::processEvents();
        CHECK(std::abs(stack.find("satellite")->opacity - 0.30f) < 0.01f);
        CHECK(opacity->toolTip().contains("30%"));
    }

    // A change made anywhere else must reach the panel, so the two can never
    // disagree about what is on screen.
    stack.setVisible("los", true);
    QApplication::processEvents();
    auto* los = panel.findChild<QCheckBox*>("LayerVisiblelos");
    CHECK(los && los->isChecked());

    for (const char* id : {"satellite", "usgs-topo", "hillshade", "vegetation",
                           "coverage", "comparison", "los", "pattern", "jamming",
                           "fresnel"}) {
        CHECK(panel.findChild<QCheckBox*>(QString("LayerVisible%1").arg(id)) != nullptr);
        CHECK(panel.findChild<QSlider*>(QString("LayerOpacity%1").arg(id)) != nullptr);
    }
    capturePanel(panel, "layers-panel");
}

int main(int argc, char** argv) {
    // Permit headless QA on Windows without changing normal native test runs.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
#ifdef Q_OS_WIN
        qputenv("QT_QPA_PLATFORM", "windows");
#else
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    }
    QApplication application(argc, argv);
#ifdef Q_OS_WIN
    if (QApplication::platformName() == "offscreen") {
        // Qt's offscreen Windows backend does not discover system fonts.
        // Load the workspace families explicitly instead of testing tofu glyphs.
        const QDir fonts(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts"));
        for (const char* name : {"segoeui.ttf", "segoeuib.ttf", "seguisym.ttf", "consola.ttf", "consolab.ttf"})
            CHECK(QFontDatabase::addApplicationFont(fonts.filePath(name)) >= 0);
    }
#endif
    application.setStyle("Fusion");
    application.setPalette(WorkspaceTheme::palette());
    CHECK(application.palette().color(QPalette::Window) == QColor("#101a29"));
    CHECK(application.palette().color(QPalette::Text) == QColor("#e2e8f0"));
    CHECK(application.palette().color(QPalette::Disabled, QPalette::Text) == QColor("#8998aa"));
    QCoreApplication::setOrganizationName("RidgelineTests");
    QCoreApplication::setApplicationName("RidgelineUiTests");
    QTemporaryDir uiStateDir;
    if (uiStateDir.isValid()) qputenv("RIDGELINE_STATE_DIR", uiStateDir.path().toUtf8());
    application.setStyleSheet(WorkspaceTheme::styleSheet());

    testCompactMapControls();
    testLayersPanelDrivesTheStack();
    testVegetationAreaDialog();
    testSourceCardsPreserveSelection();
    testAdvancedPanelRemainsEditableAndCollapsible();
    testPreciseSourcePositionDialog();
    testPointAnalysisPanelShowsExplanation();
    testScenarioComparisonPanelWorkflow();
    testCalibrationPanelStatesAndSignals();
    testCsvUsesClampedEdgeCellCenter();
    testGeoTiffExportPlacesEachCell();
    testMgrsDialogInput();
    testDebouncedCustomGeocoderSuggestions();
    testRuntimeAssetsIgnoreLaunchDirectory();

    if (failures == 0)
        std::cout << "UI interaction tests passed.\n";
    return failures == 0 ? 0 : 1;
}
