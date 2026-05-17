#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include "sim/SimEngine.h"
#include "ViewportWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Load a DTED file and replace the current terrain.
    // Shows a QMessageBox on parse failure. Safe to call at any time.
    void loadTerrain(const QString& path);

private slots:
    void onSolve();
    void onLoadTerrain();

private:
    SimEngine       m_engine;
    ViewportWidget* m_viewport    = nullptr;
    QLabel*         m_statusLabel = nullptr;
    QPushButton*    m_solveBtn    = nullptr;

    void setupScene();
    void applyTheme();
    void placeDefaultAntennas();
};
