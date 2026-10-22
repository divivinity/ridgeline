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
#include "data/CanopyLoader.h"
#include "data/ScenarioIO.h"
#include "sim/MeasurementCalibration.h"
#include "sim/ScenarioComparison.h"
#include <cstdint>
#include <cstddef>
#include <memory>
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
    void onDuplicateAntenna();
    void onAntennaSelected(int index);
    void onAntennaChanged(Antenna updated);
    void onEditAntennaPosition();
    void onAntennaMoved(int index, float worldX, float worldZ);
    void onTerrainClicked(float worldX, float worldZ);
    void onTerrainInspected(float worldX, float worldZ);
    void onLoadCanopy();
    void onClearCanopy();
    void onCanopyOptions();
    void onVegetationDrawModeChanged(bool active, int kind);
    void onVegetationAreaDrawn(float minX, float minZ, float maxX, float maxZ);
    void onVegetationAreaReshaped(int index, float minX, float minZ, float maxX, float maxZ,
                                  bool resized);
    void onVegetationAreaClicked(int index);
    void onDeleteVegetationArea();
    void onEditVegetationArea();
    void onDeleteOffTerrainVegetation();

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
    class QMenu* m_recentScenariosMenu = nullptr;
    class QMenu* m_recentLocationsMenu = nullptr;
    // Rebuilt whenever the history changes, so the menus never show a stale list.
    void refreshRecentMenus();
    void openScenarioPath(const QString& path);
    // Fingerprint of the inputs the displayed field was computed from. Empty
    // when no field is displayed.
    std::string m_solvedSignature;
    // True when a field is on screen but its inputs have since changed. Every
    // input path clears the field, so this is an invariant check rather than a
    // user-facing state; see testDisplayedResultsAlwaysMatchTheirInputs.
    bool resultsAreStale() const;

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

    // Installs an imported canopy layer on the engine as a sampler, so it is
    // re-placed on every later terrain load rather than going stale.
    void installCanopy(CanopyRaster raster);
    // Re-issues the recorded request after a scenario load or a policy change.
    // Silent on failure: a scenario must still open when the canopy cannot be
    // restored, with the reason on the status line rather than in a dialog.
    void restoreCanopyImport(const CanopyImport& record);
    void refreshCanopyActions();
    // One line naming the source, its vintage and how much of the area it
    // describes. Shown after a load and kept in the vegetation menu.
    QString canopySummary() const;

    // Shared, so the sampler handed to the engine keeps the raster alive
    // regardless of which member is destroyed first.
    std::shared_ptr<const CanopyRaster> m_canopy;
    landfire::Options m_canopyOptions;
    QAction*         m_clearCanopyAct = nullptr;
    QAction*         m_canopyStatusAct = nullptr;

    // Drawn vegetation. Every change - draw, move, resize, edit, delete, undo,
    // redo - goes through applyVegetationState, so there is one place where
    // results are invalidated and the map is brought up to date.
    void applyVegetationState(const std::vector<VegetationArea>& areas, int selected);
    void pushVegetationChange(std::vector<VegetationArea> after, int selectedAfter,
                              const QString& description);
    void refreshVegetationActions();
    // The map texture is rebuilt after a short, restartable delay and never
    // inside an undo command: the rebuild can run nested event loops, and a
    // Ctrl+Z dispatched from one while QUndoStack::push is still running would
    // corrupt the history.
    void scheduleVegetationWashRefresh();
    void flushVegetationWash();
    QString vegetationEffectSummary(const VegetationArea& area) const;
    QString vegetationToolHint(VegetationKind kind) const;
    // A new stand's height: the median imported canopy under it where at least
    // half its samples carry some, otherwise the last height set this session.
    float defaultStandHeightM(const VegetationArea& area, bool& fromImport) const;
    std::string nextVegetationName(VegetationKind kind) const;

    // Index-based, which is sound only because every apply, load and tool exit
    // re-clamps it and cancels any gesture in progress.
    int              m_selectedVegetationIndex = -1;
    QAction*         m_drawStandsAct = nullptr;
    QAction*         m_drawClearingsAct = nullptr;
    QAction*         m_editVegetationAct = nullptr;
    QAction*         m_deleteVegetationAct = nullptr;
    QAction*         m_deleteOffTerrainVegetationAct = nullptr;
    QTimer*          m_vegetationWashTimer = nullptr;
    float            m_lastStandHeightM = 15.0f;
};
