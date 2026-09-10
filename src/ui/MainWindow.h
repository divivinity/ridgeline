#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QDockWidget>
#include "sim/SimEngine.h"
#include "ViewportWidget.h"
#include "ScenarioTree.h"
#include "PropertiesPanel.h"
#include "OverlayToolbar.h"
#include "BottomBar.h"
#include "PointAnalysisPanel.h"
#include "ScenarioComparisonPanel.h"
#include "CalibrationPanel.h"
#include "data/AntennaLibrary.h"
#include "data/ScenarioIO.h"
#include "sim/MeasurementCalibration.h"
#include "sim/ScenarioComparison.h"
#include <cstdint>
#include <cstddef>
#include <limits>

#include "map/LayerStack.h"

class QUndoStack;
class LayersPanel;
class QProgressDialog;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void loadTerrain(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSolve();
    void onChooseLocation();
    void onReceiverSettings();
    void onLoadTerrain();
    void onSaveScenario();
    void onOpenScenario();
    void onImportMeasurements();
    void onExportPng();
    void onExportCsv();
    void onAddAntenna();
    void onAddJammer();
    void onRemoveAntenna(int index);
    void onAntennaSelected(int index);
    void onAntennaChanged(Antenna updated);
    void onEditAntennaPosition();
    void onAntennaMoved(int index, float worldX, float worldZ);
    void onTerrainClicked(float worldX, float worldZ);
    void onTerrainInspected(float worldX, float worldZ);

private:
    SimEngine        m_engine;
    AntennaLibrary   m_antennaLib;
    TerrainSource    m_terrainSource;
    ViewportWidget*  m_viewport = nullptr;
    ScenarioTree*    m_scenarioTree = nullptr;
    PropertiesPanel* m_propsPanel = nullptr;
    OverlayToolbar*  m_overlayBar = nullptr;
    BottomBar*       m_bottomBar = nullptr;
    PointAnalysisPanel* m_pointPanel = nullptr;
    QDockWidget*     m_pointDock = nullptr;
    ScenarioComparisonPanel* m_comparisonPanel = nullptr;
    QDockWidget*     m_comparisonDock = nullptr;
    CalibrationPanel* m_calibrationPanel = nullptr;
    QDockWidget*     m_calibrationDock = nullptr;
    QLabel*          m_statusLabel = nullptr;
    QLabel*          m_locationLabel = nullptr;
    QPushButton*     m_solveBtn = nullptr;
    QComboBox*       m_modeCombo = nullptr;
    int              m_selectedIndex = -1;
    Antenna          m_propsAntennaView;
    bool             m_scenarioDirty = false;  // non-undoable unsaved changes
    bool             m_solveInProgress = false;  // a coverage run owns the UI
    bool             m_terrainLoadInProgress = false;
    LayerStack       m_layerStack;
    LayersPanel*     m_layersPanel = nullptr;
    QDockWidget*     m_layersDock = nullptr;
    // The dock arrangement as built, so a workspace dragged into a mess can be
    // put back without restarting.
    QByteArray       m_defaultLayout;
    bool             m_placementActive = false;
    bool             m_placingJammer = false;
    QString          m_locationName;
    QUndoStack*      m_undoStack = nullptr;
    FieldGrid        m_baselineField;
    std::uint64_t    m_terrainRevision = 0;
    std::uint64_t    m_baselineTerrainRevision = 0;
    std::vector<RFMeasurement> m_measurements;
    CalibrationReport m_calibrationReport;
    QString          m_measurementPath;
    std::size_t      m_measurementRowsRead = 0;
    std::size_t      m_measurementRejectedRows = 0;
    QStringList      m_measurementImportWarnings;
    bool             m_calibrationReportCurrent = false;
    QString          m_appliedCalibrationSourceName;
    std::size_t      m_appliedCalibrationPairedSamples = 0;
    float            m_appliedCalibrationRmseBeforeDb =
        std::numeric_limits<float>::quiet_NaN();
    float            m_appliedCalibrationRmseAfterDb =
        std::numeric_limits<float>::quiet_NaN();

    // Unsaved-state tracking. Source edits go through the undo stack, so its
    // clean flag covers them (and goes clean again if the user undoes back to
    // the last save). Terrain, mode, receiver and calibration changes are not
    // undoable and loadTerrain() clears the stack, so they are tracked apart.
    bool isScenarioDirty() const;
    void markScenarioDirty();
    void markScenarioClean();
    // Prompts to save when there are unsaved changes. Returns false if the
    // caller should abandon what it was about to do.
    bool confirmDiscardChanges();

    void setupScene();
    void applyTheme();
    void placeDefaultAntennas();
    void loadGlobalLocation(double latitude, double longitude,
                            double extentKm, const QString& name = {});
    void startPlacement(bool jammer);
    void updateLocationSummary();
    void refreshTree();
    void refreshPropsPanel();
    void refreshBottomBar();
    // The current workspace as a document. Shared by Save and by autosave, so
    // a recovered workspace can never differ in shape from a saved one.
    ScenarioDoc currentScenarioDoc() const;
    // Writes a recovery copy when there is unsaved work and the workspace is
    // idle. Never interrupts: a failed write just waits for the next tick.
    void autosaveTick();
    // Offers to restore a recovery copy left by a previous session.
    void offerRecovery();
    QTimer* m_autosaveTimer = nullptr;

    void applyScenarioDoc(const ScenarioDoc& doc, QProgressDialog& progress,
                          bool& terrainInstalled);
    void applySourceState(const std::vector<Antenna>& antennas, int selectedIndex);
    void pushSourceChange(std::vector<Antenna> after, int selectedAfter,
                          const QString& description, int mergeId = -1);
    void syncAntennaGeo(Antenna& antenna) const;
    QString sourcePositionSummary(const Antenna& antenna) const;
    void captureScenarioBaseline();
    void clearScenarioBaseline();
    void invalidateScenarioComparisonCurrent();
    void resetScenarioComparisonForTerrain();
    void updateScenarioComparisonAfterSolve();
    void showCalibrationPanel();
    CalibrationPanel::Report calibrationAwaitingReport() const;
    void updateCalibrationAfterSolve();
    void invalidateCalibrationCurrent();
    void applyCalibrationCorrection();
    void resetCalibrationCorrection();
    void clearCalibrationMeasurements();
    void handleCalibrationTerrainChange();
};
