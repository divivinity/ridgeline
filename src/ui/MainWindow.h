#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include "sim/SimEngine.h"
#include "ViewportWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void loadTerrain(const QString& path);

private slots:
    void onSolve();
    void onLoadTerrain();

private:
    SimEngine       m_engine;
    ViewportWidget* m_viewport    = nullptr;
    QLabel*         m_statusLabel = nullptr;
    QPushButton*    m_solveBtn    = nullptr;
    QComboBox*      m_modeCombo   = nullptr;

    void setupScene();
    void applyTheme();
    void placeDefaultAntennas();
};
