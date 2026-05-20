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
#include "BottomBar.h"                // ← add
#include "data/AntennaLibrary.h"
#include "data/ScenarioIO.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void loadTerrain(const QString& path);

private slots:
    void onSolve();
    void onLoadTerrain();
    void onSaveScenario();
    void onOpenScenario();
    void onExportPng();
    void onExportCsv();
    void onAddAntenna();
    void onRemoveAntenna(int index);
    void onAntennaSelected(int index);
    void onAntennaChanged(Antenna updated);

private:
    SimEngine        m_engine;
    AntennaLibrary   m_antennaLib;
    TerrainSource    m_terrainSource;   // tracks what terrain is currently loaded
    ViewportWidget*  m_viewport      = nullptr;
    ScenarioTree*    m_scenarioTree  = nullptr;
    PropertiesPanel* m_propsPanel    = nullptr;
    OverlayToolbar*  m_overlayBar    = nullptr;
    BottomBar*       m_bottomBar     = nullptr;  // ← add
    QLabel*          m_statusLabel   = nullptr;
    QPushButton*     m_solveBtn      = nullptr;
    QComboBox*       m_modeCombo     = nullptr;
    int              m_selectedIndex = -1;
    Antenna          m_propsAntennaView;

    void setupScene();
    void applyTheme();
    void placeDefaultAntennas();
    void refreshTree();
    void refreshPropsPanel();
    void refreshBottomBar();
    void applyScenarioDoc(const ScenarioDoc& doc);
};
