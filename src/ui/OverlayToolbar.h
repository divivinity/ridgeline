#pragma once
#include <QWidget>
#include <QBoxLayout>
#include "render/overlays/HeatmapOverlay.h"

// Map controls that are not layers: which coverage metric is displayed, and the
// camera shortcuts. Layer visibility, opacity and order live in the Layers dock
// (LayersPanel), which is the single place that owns them — this toolbar
// deliberately keeps no layer state so the two cannot disagree.
class OverlayToolbar : public QWidget {
    Q_OBJECT
public:
    explicit OverlayToolbar(QWidget* parent = nullptr);

public slots:
    // Kept in step when the viewport cancels the ruler itself, e.g. on Escape.
    void setMeasureChecked(bool checked);

signals:
    void heatmapMetricChanged(HeatmapOverlay::Metric metric);
    void measureModeToggled(bool active);
    void resetViewRequested();
    void topViewRequested();

private:
    class QToolButton* m_measureButton = nullptr;
};
