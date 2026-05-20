#pragma once
#include <QWidget>
#include <vector>
#include "sim/SpectrumCalc.h"
#include "sim/Types.h"

// Bottom-dock spectrum display.
// Call setAntenna() when the user selects an antenna.
// Call clearAntenna() when nothing is selected.
// Paints a Friis FSPL sweep from 30–3000 MHz at a 1 km reference distance.
class BottomBar : public QWidget {
    Q_OBJECT
public:
    explicit BottomBar(QWidget* parent = nullptr);

    void setAntenna(const Antenna& antenna);
    void clearAntenna();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<SpectrumPoint> m_spectrum;
    bool m_hasAntenna = false;

    static constexpr float kMinDbm = -120.0f;
    static constexpr float kMaxDbm =  -30.0f;
};
