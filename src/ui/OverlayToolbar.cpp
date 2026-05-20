#include "OverlayToolbar.h"
#include <QHBoxLayout>
#include <QLabel>

OverlayToolbar::OverlayToolbar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(12);

    auto* overlaysLabel = new QLabel("Overlays:");
    overlaysLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-family: monospace; font-size: 11px;");
    layout->addWidget(overlaysLabel);

    buildCheckbox("Basemap",  ViewportWidget::Basemap, layout);
    buildCheckbox("Heatmap",  ViewportWidget::Heatmap, layout);
    buildCheckbox("LOS Rays", ViewportWidget::LOS,     layout);
    buildCheckbox("Lobe",     ViewportWidget::Lobe,    layout);
    buildCheckbox("Jammer",   ViewportWidget::Jammer,  layout);
    buildCheckbox("Fresnel",  ViewportWidget::Fresnel, layout);

    layout->addStretch();
    setStyleSheet("background: rgba(15,25,35,0.6); border-bottom: 1px solid rgba(255,255,255,0.06);");
}

void OverlayToolbar::buildCheckbox(const QString& label,
                                   ViewportWidget::Overlay id,
                                   QBoxLayout* layout) {
    auto* cb = new QCheckBox(label);
    cb->setChecked(true);
    cb->setStyleSheet(
        "QCheckBox { color: rgba(255,255,255,0.75); font-family: monospace; font-size: 11px; }"
        "QCheckBox::indicator { width: 13px; height: 13px; }"
        "QCheckBox::indicator:checked { background: #4fc3f7; border: 1px solid #4fc3f7; border-radius: 2px; }"
        "QCheckBox::indicator:unchecked { background: transparent; border: 1px solid rgba(255,255,255,0.3); border-radius: 2px; }"
    );
    connect(cb, &QCheckBox::toggled, this, [this, id](bool checked) {
        emit overlayToggled(id, checked);
    });
    layout->addWidget(cb);
}
