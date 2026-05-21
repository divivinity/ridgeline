#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QKeyEvent>
#include <QPoint>
#include "render/Camera.h"
#include "render/TerrainMesh.h"
#include "render/overlays/HeatmapOverlay.h"
#include "render/overlays/LOSRayOverlay.h"
#include "render/overlays/AntennaLobeOverlay.h"
#include "render/overlays/JammerBubbleOverlay.h"
#include "render/overlays/FresnelOverlay.h"
#include "render/BasemapOverlay.h"
#include "sim/SimEngine.h"

class ViewportWidget : public QOpenGLWidget,
                       protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    enum Overlay { Basemap = 0, Heatmap = 1, LOS = 2, Lobe = 3, Jammer = 4, Fresnel = 5 };

    explicit ViewportWidget(SimEngine* engine, QWidget* parent = nullptr);
    void refreshOverlays();   // call after SimEngine::solve()
    void reloadTerrain();     // call after SimEngine::setTerrain() to rebuild GPU meshes
    void setOverlayVisible(Overlay overlay, bool visible);

signals:
    void terrainClicked(float worldX, float worldZ);

protected:
    void initializeGL()              override;
    void resizeGL(int w, int h)      override;
    void paintGL()                   override;
    void mousePressEvent(QMouseEvent*)   override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*)    override;
    void wheelEvent(QWheelEvent*)        override;
    void keyPressEvent(QKeyEvent*)       override;  // T=top-down, R=reset, arrows=orbit

private:
    SimEngine*    m_engine;
    Camera        m_camera;
    Camera        m_defaultCamera;  // saved at initializeGL/reloadTerrain for R key
    TerrainMesh   m_terrain;
    BasemapOverlay    m_basemap;
    HeatmapOverlay     m_heatmap;
    LOSRayOverlay      m_losRay;
    AntennaLobeOverlay m_antennaLobe;
    JammerBubbleOverlay m_jammerBubble;
    FresnelOverlay     m_fresnel;
    QPoint             m_lastMouse;
    QPoint             m_pressPos;   // for distinguishing click vs drag
    bool          m_dragging = false;

    bool raycastTerrain(const QPoint& screenPos,
                        float& outWorldX, float& outWorldZ) const;
};
