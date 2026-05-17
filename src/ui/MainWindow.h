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

private slots:
    void onSolve();

private:
    SimEngine      m_engine;
    ViewportWidget* m_viewport;
    QLabel*         m_statusLabel;
    QPushButton*    m_solveBtn;

    void setupScene();   // load demo terrain + 2 antennas
    void applyTheme();   // glassmorphism stylesheet
};
