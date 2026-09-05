#include "OverlayToolbar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QComboBox>

OverlayToolbar::OverlayToolbar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(10);

    auto* overlaysLabel = new QLabel("VIEW");
    overlaysLabel->setStyleSheet("color: #64748b; font-size: 9px; font-weight: 700; letter-spacing: 1px;");
    layout->addWidget(overlaysLabel);

    buildCheckbox("Satellite", ViewportWidget::Basemap, layout);
    buildCheckbox("Coverage", ViewportWidget::Heatmap, layout);

    auto* metric = new QComboBox();
    metric->addItem("Desired dBm", static_cast<int>(HeatmapOverlay::Metric::DesiredPower));
    metric->addItem("Jammer dBm", static_cast<int>(HeatmapOverlay::Metric::JammerPower));
    metric->addItem("SNR", static_cast<int>(HeatmapOverlay::Metric::Snr));
    metric->addItem("SINR", static_cast<int>(HeatmapOverlay::Metric::Sinr));
    metric->addItem("J/S", static_cast<int>(HeatmapOverlay::Metric::JOverS));
    metric->addItem("Fade margin", static_cast<int>(HeatmapOverlay::Metric::FadeMargin));
    metric->addItem("Service contour", static_cast<int>(HeatmapOverlay::Metric::Service));
    metric->setToolTip("Coverage field displayed on the terrain");
    metric->setStyleSheet(
        "QComboBox { background: #111c2e; border: 1px solid #2a3b52; border-radius: 5px;"
        " color: #cbd5e1; padding: 3px 7px; min-width: 105px; }"
    );
    connect(metric, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, metric](int index) {
        emit heatmapMetricChanged(static_cast<HeatmapOverlay::Metric>(
            metric->itemData(index).toInt()));
    });
    layout->addWidget(metric);

    auto* opacityLabel = new QLabel("Map");
    opacityLabel->setStyleSheet("color: #64748b; font-size: 10px;");
    layout->addWidget(opacityLabel);
    auto* opacity = new QSlider(Qt::Horizontal);
    opacity->setRange(20, 100);
    opacity->setValue(86);
    opacity->setFixedWidth(90);
    opacity->setToolTip("Satellite imagery opacity");
    connect(opacity, &QSlider::valueChanged, this, [this](int value) {
        emit basemapOpacityChanged(value / 100.0f);
    });
    layout->addWidget(opacity);

    auto* advancedButton = new QToolButton();
    advancedButton->setText("Analysis layers");
    advancedButton->setCheckable(true);
    advancedButton->setArrowType(Qt::RightArrow);
    advancedButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    layout->addWidget(advancedButton);

    auto* advanced = new QWidget();
    auto* advancedLayout = new QHBoxLayout(advanced);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(9);
    buildCheckbox("LOS", ViewportWidget::LOS, advancedLayout, false);
    buildCheckbox("Pattern", ViewportWidget::Lobe, advancedLayout, false);
    buildCheckbox("Jamming", ViewportWidget::Jammer, advancedLayout, false);
    buildCheckbox("Fresnel", ViewportWidget::Fresnel, advancedLayout, false);
    advanced->hide();
    connect(advancedButton, &QToolButton::toggled, this,
            [advancedButton, advanced](bool shown) {
        advancedButton->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
        advanced->setVisible(shown);
    });
    layout->addWidget(advanced);

    layout->addStretch();
    setStyleSheet(
        "QWidget { background: transparent; }"
        "QToolButton { color: #94a3b8; border: none; padding: 3px 7px; }"
        "QToolButton:hover { color: #e2e8f0; background: #172338; border-radius: 5px; }"
        "QSlider::groove:horizontal { height: 3px; background: #26364d; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #2dd4bf; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 11px; margin: -4px 0; background: #ccfbf1; border-radius: 5px; }"
    );
}

void OverlayToolbar::buildCheckbox(const QString& label,
                                   ViewportWidget::Overlay id,
                                   QBoxLayout* layout,
                                   bool checked) {
    auto* cb = new QCheckBox(label);
    cb->setChecked(checked);
    cb->setStyleSheet(
        "QCheckBox { color: #cbd5e1; font-size: 11px; spacing: 5px; }"
        "QCheckBox::indicator { width: 13px; height: 13px; }"
        "QCheckBox::indicator:checked { background: #14b8a6; border: 1px solid #2dd4bf; border-radius: 3px; }"
        "QCheckBox::indicator:unchecked { background: #111c2e; border: 1px solid #3b4b63; border-radius: 3px; }"
    );
    connect(cb, &QCheckBox::toggled, this, [this, id](bool checked) {
        emit overlayToggled(id, checked);
    });
    layout->addWidget(cb);
}
