#pragma once
#include <QWidget>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include "sim/Types.h"

class AntennaLibrary;

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);
    void setAntennaLibrary(const AntennaLibrary* lib);
    void load(const Antenna* antenna);
    void setPositionSummary(const QString& summary);

signals:
    void antennaChanged(Antenna updated);
    void positionEditRequested();

private slots:
    void onValueChanged();
    void onTypeChanged(int index);
    void onImportPattern();

private:
    QLabel*         m_noSelLabel  = nullptr;
    QScrollArea*    m_scrollArea  = nullptr;
    QWidget*        m_form        = nullptr;
    QWidget*        m_advancedPanel = nullptr;
    QToolButton*    m_advancedToggle = nullptr;
    QComboBox*      m_typeCombo   = nullptr;
    QDoubleSpinBox* m_freqSpin    = nullptr;
    QDoubleSpinBox* m_powerSpin   = nullptr;
    QDoubleSpinBox* m_gainSpin    = nullptr;
    QDoubleSpinBox* m_heightSpin  = nullptr;
    QDoubleSpinBox* m_azimuthSpin = nullptr;
    QDoubleSpinBox* m_downtiltSpin = nullptr;
    QDoubleSpinBox* m_hBeamSpin = nullptr;
    QDoubleSpinBox* m_vBeamSpin = nullptr;
    QDoubleSpinBox* m_frontBackSpin = nullptr;
    QDoubleSpinBox* m_cableLossSpin = nullptr;
    QComboBox*      m_polarizationCombo = nullptr;
    QCheckBox*      m_jammerCheck = nullptr;
    QPushButton*    m_importPatternButton = nullptr;
    QLabel*         m_patternLabel = nullptr;
    QLabel*         m_eirpLabel = nullptr;
    QLabel*         m_erpLabel = nullptr;
    QLabel*         m_positionLabel = nullptr;
    QPushButton*    m_editPositionButton = nullptr;

    const AntennaLibrary* m_lib = nullptr;
    bool                  m_loading = false;
    Antenna               m_current;

    QDoubleSpinBox* makeSpinBox(double min, double max, double step,
                                const QString& suffix);
    void updatePowerReadout();
};
