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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void loadTerrain(const QString& path);

private slots:
    void onSolve();
    void onLoadTerrain();
    void onAddAntenna();
    void onRemoveAntenna(int index);
    void onAntennaSelected(int index);
    void onAntennaChanged(Antenna updated);

private:
    SimEngine        m_engine;
    ViewportWidget*  m_viewport      = nullptr;
    ScenarioTree*    m_scenarioTree  = nullptr;
    PropertiesPanel* m_propsPanel    = nullptr;
    OverlayToolbar*  m_overlayBar    = nullptr;
    QLabel*          m_statusLabel   = nullptr;
    QPushButton*     m_solveBtn      = nullptr;
    QComboBox*       m_modeCombo     = nullptr;
    int              m_selectedIndex = -1;

    void setupScene();
    void applyTheme();
    void placeDefaultAntennas();
    void refreshTree();
    void refreshPropsPanel();
};
