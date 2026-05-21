#pragma once
#include <QWidget>

// Floating semi-transparent legend that shows the heatmap colour scale.
// Place as a child of the viewport; call reposition() from resizeGL.
class HeatmapLegend : public QWidget {
    Q_OBJECT
public:
    explicit HeatmapLegend(QWidget* parent = nullptr);
    void reposition(const QSize& parentSize);   // call on parent resize

protected:
    void paintEvent(QPaintEvent*) override;
};
