#include "OverlayToolbar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QComboBox>
#include <QMenu>

OverlayToolbar::OverlayToolbar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* imagery = buildCheckbox("Imagery", ViewportWidget::Basemap, layout);
    buildCheckbox("Coverage", ViewportWidget::Heatmap, layout);

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
    metric->setToolTip("Choose the coverage result shown on the terrain");
    connect(metric, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, metric](int index) {
        emit heatmapMetricChanged(static_cast<HeatmapOverlay::Metric>(
            metric->itemData(index).toInt()));
    });
    layout->addWidget(metric);

    auto* opacity = new QSlider(Qt::Horizontal);
    opacity->setObjectName("ImageryOpacitySlider");
    opacity->setAccessibleName("Imagery opacity");
    opacity->setRange(20, 100);
    opacity->setValue(86);
    opacity->setFixedWidth(80);
    auto* opacityLabel = new QLabel("86%");
    opacityLabel->setObjectName("ImageryOpacityValue");
    opacityLabel->setFixedWidth(36);
    opacityLabel->setStyleSheet("color: #a8b7ca;");
    opacity->setToolTip("Imagery opacity: 86%");
    connect(opacity, &QSlider::valueChanged, this, [this, opacity, opacityLabel](int value) {
        opacityLabel->setText(QString::number(value) + "%");
        opacity->setToolTip(QString("Imagery opacity: %1%").arg(value));
        emit basemapOpacityChanged(value / 100.0f);
    });
    layout->addWidget(opacity);
    layout->addWidget(opacityLabel);
    const auto updateOpacityAvailability = [imagery, opacity, opacityLabel]() {
        opacity->setEnabled(imagery->isChecked());
        opacityLabel->setEnabled(imagery->isChecked());
    };
    connect(imagery, &QCheckBox::toggled, this, updateOpacityAvailability);
    updateOpacityAvailability();

    auto* layers = new QToolButton();
    layers->setObjectName("AnalysisLayersButton");
    layers->setText("Layers");
    layers->setToolTip("Additional analysis overlays");
    layers->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(layers);
    const auto addLayer = [this, menu](const QString& text, ViewportWidget::Overlay id) {
        auto* action = menu->addAction(text);
        action->setObjectName(QString("AnalysisLayer%1").arg(static_cast<int>(id)));
        action->setCheckable(true);
        connect(action, &QAction::toggled, this, [this, id](bool checked) {
            emit overlayToggled(id, checked);
        });
    };
    addLayer("Line of sight", ViewportWidget::LOS);
    addLayer("Antenna pattern", ViewportWidget::Lobe);
    addLayer("Jamming range", ViewportWidget::Jammer);
    addLayer("Fresnel zone", ViewportWidget::Fresnel);
    layers->setMenu(menu);
    layout->addWidget(layers);
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

    setStyleSheet(
        "QSlider { background: transparent; border: none; min-height: 22px; }"
        "QSlider::groove:horizontal { height: 4px; border: none; background: #36465c; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { border: none; background: #4fd1bd; border-radius: 2px; }"
        "QSlider::add-page:horizontal { border: none; background: #36465c; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -5px 0; border: none; background: #ccfbf1; border-radius: 6px; }"
        "QSlider::handle:horizontal:focus { background: white; border: 1px solid #4fd1bd; }"
        "QSlider::sub-page:horizontal:disabled { background: #36465c; }"
        "QSlider::handle:horizontal:disabled { background: #64748b; }"
    );
}

QCheckBox* OverlayToolbar::buildCheckbox(const QString& label,
                                   ViewportWidget::Overlay id,
                                   QBoxLayout* layout,
                                   bool checked) {
    auto* cb = new QCheckBox(label);
    cb->setObjectName(QString("OverlayToggle%1").arg(static_cast<int>(id)));
    cb->setChecked(checked);
    cb->setToolTip(id == ViewportWidget::Basemap
        ? "Show satellite imagery when available for the loaded location"
        : "Show the selected coverage metric after a solve");
    connect(cb, &QCheckBox::toggled, this, [this, id](bool visible) {
        emit overlayToggled(id, visible);
    });
    layout->addWidget(cb);
    return cb;
}
