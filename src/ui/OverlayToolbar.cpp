#include "OverlayToolbar.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

OverlayToolbar::OverlayToolbar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* metricLabel = new QLabel("Coverage");
    metricLabel->setStyleSheet("color: #a8b7ca; font-size: 12px;");
    layout->addWidget(metricLabel);

    auto* metric = new QComboBox();
    metric->setObjectName("CoverageMetricCombo");
    metric->setAccessibleName("Coverage metric");
    metric->addItem("Desired power · dBm", static_cast<int>(HeatmapOverlay::Metric::DesiredPower));
    metric->addItem("Jammer power · dBm", static_cast<int>(HeatmapOverlay::Metric::JammerPower));
    metric->addItem("SNR · dB", static_cast<int>(HeatmapOverlay::Metric::Snr));
    metric->addItem("SINR · dB", static_cast<int>(HeatmapOverlay::Metric::Sinr));
    metric->addItem("J/S · dB", static_cast<int>(HeatmapOverlay::Metric::JOverS));
    metric->addItem("Fade margin · dB", static_cast<int>(HeatmapOverlay::Metric::FadeMargin));
    metric->addItem("Service area", static_cast<int>(HeatmapOverlay::Metric::Service));
    // This selects what the Coverage layer shows, not whether it is visible,
    // which is why it stays here rather than moving into the Layers dock.
    metric->setToolTip("Choose the coverage result shown on the terrain");
    connect(metric, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, metric](int index) {
        emit heatmapMetricChanged(static_cast<HeatmapOverlay::Metric>(
            metric->itemData(index).toInt()));
    });
    layout->addWidget(metric);
    layout->addStretch();

    auto* top = new QToolButton();
    top->setObjectName("TopMapViewButton");
    top->setText("Top view");
    top->setToolTip("Look down at the terrain (T while the map is focused)");
    connect(top, &QToolButton::clicked, this, &OverlayToolbar::topViewRequested);
    layout->addWidget(top);

    auto* fit = new QToolButton();
    fit->setObjectName("ResetMapViewButton");
    fit->setText("Reset view");
    fit->setToolTip("Return to the default terrain view (R while the map is focused)");
    connect(fit, &QToolButton::clicked, this, &OverlayToolbar::resetViewRequested);
    layout->addWidget(fit);
}
