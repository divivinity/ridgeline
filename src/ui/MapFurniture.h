#pragma once
#include <QWidget>

#include "map/MapMeasure.h"

// Map furniture: the fixed reference marks a map needs to be read rather than
// just looked at. Both are plain painted widgets parented to the viewport, so
// they compose into an exported PNG the same way the legend does.

// A bar showing how far a screen distance represents on the ground.
class MapScaleBar : public QWidget {
    Q_OBJECT
public:
    explicit MapScaleBar(QWidget* parent = nullptr);

    // Metres of ground per screen pixel at the camera's focus. A non-positive
    // or non-finite value hides the bar rather than drawing a false scale.
    void setMetresPerPixel(double metresPerPixel);
    double metresPerPixel() const { return m_metresPerPixel; }
    // The distance the bar currently represents, for tests and captures.
    double barMetres() const { return m_bar.metres; }
    QString barLabel() const { return m_bar.label; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void recompute();

    double m_metresPerPixel = 0.0;
    MapMeasure::ScaleBar m_bar;
};

// An arrow pointing to true north for the current camera azimuth.
class NorthIndicator : public QWidget {
    Q_OBJECT
public:
    explicit NorthIndicator(QWidget* parent = nullptr);

    // Camera azimuth in degrees, as Camera defines it.
    void setCameraAzimuth(double azimuthDeg);
    double cameraAzimuth() const { return m_azimuthDeg; }

    // Where north sits on screen, in degrees clockwise from straight up.
    //
    // Camera places the eye at +Z for azimuth zero and looks back at the
    // target, so the view faces -Z. Terrain rows run south to north with
    // increasing Z, which means at azimuth zero the camera is looking SOUTH
    // and north is behind the viewer, at the bottom of the screen. Hence the
    // 180 degree term: an arrow drawn at -azimuth points exactly backwards.
    double northScreenAngleDeg() const;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    double m_azimuthDeg = 0.0;
};
