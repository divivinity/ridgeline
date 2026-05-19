#pragma once
#include <QWidget>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include "sim/Types.h"

class AntennaLibrary;

// Right-dock panel for editing a selected antenna's parameters.
// MainWindow calls load() when selection changes.
// When the user edits a field, antennaChanged() fires with the full updated Antenna.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    // Call once after construction to populate the type combo.
    // Safe to call again if the library reloads (rebuilds combo).
    void setAntennaLibrary(const AntennaLibrary* lib);

    // Populate controls from the given antenna. Pass nullptr to clear/disable.
    void load(const Antenna* antenna);

signals:
    void antennaChanged(Antenna updated);

private slots:
    void onValueChanged();
    void onTypeChanged(int index);

private:
    QLabel*         m_noSelLabel  = nullptr;
    QWidget*        m_form        = nullptr;
    QComboBox*      m_typeCombo   = nullptr;
    QDoubleSpinBox* m_freqSpin    = nullptr;
    QDoubleSpinBox* m_powerSpin   = nullptr;
    QDoubleSpinBox* m_gainSpin    = nullptr;
    QDoubleSpinBox* m_heightSpin  = nullptr;
    QCheckBox*      m_jammerCheck = nullptr;

    const AntennaLibrary* m_lib     = nullptr;
    bool                  m_loading = false;   // suppress signals while loading values
    Antenna               m_current;           // last loaded antenna (position preserved)

    QDoubleSpinBox* makeSpinBox(double min, double max, double step,
                                const QString& suffix);
};
