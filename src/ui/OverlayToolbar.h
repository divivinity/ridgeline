#pragma once
#include <QWidget>
#include <QCheckBox>
#include <QBoxLayout>
#include "ViewportWidget.h"

class OverlayToolbar : public QWidget {
    Q_OBJECT
public:
    explicit OverlayToolbar(QWidget* parent = nullptr);

signals:
    void overlayToggled(ViewportWidget::Overlay overlay, bool visible);
    void basemapOpacityChanged(float opacity);
    void heatmapMetricChanged(HeatmapOverlay::Metric metric);
    void resetViewRequested();
    void topViewRequested();

private:
    QCheckBox* buildCheckbox(const QString& label, ViewportWidget::Overlay id,
                       QBoxLayout* layout, bool checked = true);
};
