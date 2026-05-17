#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QPoint>
#include "render/Camera.h"
#include "render/TerrainMesh.h"
#include "render/overlays/HeatmapOverlay.h"
#include "sim/SimEngine.h"

class ViewportWidget : public QOpenGLWidget,
                       protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit ViewportWidget(SimEngine* engine, QWidget* parent = nullptr);
    void refreshOverlays();   // call after SimEngine::solve()
    void reloadTerrain();     // call after SimEngine::setTerrain() to rebuild GPU meshes

protected:
    void initializeGL()              override;
    void resizeGL(int w, int h)      override;
    void paintGL()                   override;
    void mousePressEvent(QMouseEvent*)   override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*)    override;
    void wheelEvent(QWheelEvent*)        override;

private:
    SimEngine*    m_engine;
    Camera        m_camera;
    TerrainMesh   m_terrain;
    HeatmapOverlay m_heatmap;
    QPoint        m_lastMouse;
    bool          m_dragging = false;
};
