#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QKeyEvent>
#include <QPoint>
#include "render/Camera.h"
#include "render/TerrainMesh.h"
#include "render/ShaderProgram.h"
#include "render/overlays/HeatmapOverlay.h"
#include "render/overlays/LOSRayOverlay.h"
#include "render/overlays/AntennaLobeOverlay.h"
#include "render/overlays/JammerBubbleOverlay.h"
#include "render/overlays/FresnelOverlay.h"
#include "render/BasemapOverlay.h"
#include "sim/SimEngine.h"
#include "HeatmapLegend.h"
#include <glm/glm.hpp>
#include <functional>

class QLabel;

class ViewportWidget : public QOpenGLWidget,
                       protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    enum Overlay { Basemap = 0, Heatmap = 1, LOS = 2, Lobe = 3, Jammer = 4, Fresnel = 5 };

    explicit ViewportWidget(SimEngine* engine, QWidget* parent = nullptr);
    void refreshOverlays();   // call after SimEngine::solve()
    void reloadTerrain();     // call after SimEngine::setTerrain() to rebuild GPU meshes
    void setOverlayVisible(Overlay overlay, bool visible);
    void setBasemapOpacity(float opacity);
    void setPlacementMode(bool active, bool jammer = false);
    void setBasemapLoadCallbacks(BasemapOverlay::ProgressCallback progress,
                                 BasemapOverlay::CancelCallback cancel) {
        m_basemap.setLoadCallbacks(std::move(progress), std::move(cancel));
    }

signals:
    void terrainClicked(float worldX, float worldZ);
    void basemapInfoChanged(QString provider, QString attribution, QString error);
    void placementCancelled();

protected:
    void initializeGL()              override;
    void resizeGL(int w, int h)      override;
    void paintGL()                   override;
    void mousePressEvent(QMouseEvent*)   override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*)    override;
    void wheelEvent(QWheelEvent*)        override;
    void keyPressEvent(QKeyEvent*)       override;

private:
    SimEngine*    m_engine;
    Camera        m_camera;
    Camera        m_defaultCamera;
    TerrainMesh   m_terrain;
    BasemapOverlay    m_basemap;
    HeatmapOverlay     m_heatmap;
    LOSRayOverlay      m_losRay;
    AntennaLobeOverlay m_antennaLobe;
    JammerBubbleOverlay m_jammerBubble;
    FresnelOverlay     m_fresnel;
    QPoint             m_lastMouse;
    QPoint             m_pressPos;
    bool          m_dragging = false;
    HeatmapLegend* m_heatmapLegend = nullptr;
    QLabel*       m_mapInfoLabel = nullptr;
    QLabel*       m_attributionLabel = nullptr;
    QLabel*       m_placementHint = nullptr;
    bool          m_placementActive = false;
    bool          m_placingJammer = false;
    glm::mat4      m_lastMVP{1.0f};

    static constexpr float kVertScale = 2.5f;

    void drawAntennaMarkers();
    void buildMarkers();
    bool raycastTerrain(const QPoint& screenPos,
                        float& outWorldX, float& outWorldZ) const;
    void updateMapHud();
    void repositionHud();

    GLuint        m_markerVAO   = 0;
    GLuint        m_markerVBO   = 0;
    int           m_markerCount = 0;
    ShaderProgram m_markerShader;
};
