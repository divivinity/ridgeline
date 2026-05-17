#pragma once
#include <QWidget>
#include <QCheckBox>
#include <QBoxLayout>
#include "ViewportWidget.h"

// A row of five checkboxes that toggle the five render overlays.
// Connect overlayToggled() to ViewportWidget::setOverlayVisible().
class OverlayToolbar : public QWidget {
    Q_OBJECT
public:
    explicit OverlayToolbar(QWidget* parent = nullptr);

signals:
    void overlayToggled(ViewportWidget::Overlay overlay, bool visible);

private:
    void buildCheckbox(const QString& label, ViewportWidget::Overlay id,
                       QBoxLayout* layout);
};
