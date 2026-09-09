#pragma once
#include <QWidget>
#include "render/overlays/HeatmapOverlay.h"

// Floating semi-transparent legend that shows the heatmap colour scale.
// Place as a child of the viewport; call reposition() from resizeGL.
class HeatmapLegend : public QWidget {
    Q_OBJECT
public:
    explicit HeatmapLegend(QWidget* parent = nullptr);
    void reposition(const QSize& parentSize);   // call on parent resize
    void setMetric(HeatmapOverlay::Metric metric) { m_metric = metric; update(); }

protected:
    void paintEvent(QPaintEvent*) override;
private:
    HeatmapOverlay::Metric m_metric = HeatmapOverlay::Metric::DesiredPower;
};
