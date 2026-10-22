#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QKeyEvent>
#include <QPoint>
#include <QStringList>
#include "render/Camera.h"
#include "render/TerrainMesh.h"
#include "render/ShaderProgram.h"
#include "render/overlays/HeatmapOverlay.h"
#include "render/overlays/LOSRayOverlay.h"
#include "render/overlays/AntennaLobeOverlay.h"
#include "render/overlays/JammerBubbleOverlay.h"
#include "render/overlays/FresnelOverlay.h"
#include "render/overlays/VegetationAreaOverlay.h"
#include "render/BasemapOverlay.h"
#include "map/LayerStack.h"
#include "sim/SimEngine.h"
#include "HeatmapLegend.h"
#include "MapFurniture.h"
#include <glm/glm.hpp>
#include <functional>
#include <cstdint>
#include <utility>
#include <vector>

class QLabel;

class ViewportWidget : public QOpenGLWidget,
                       protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit ViewportWidget(SimEngine* engine, QWidget* parent = nullptr);
    void refreshOverlays();
    void reloadTerrain();

    // The stack is owned by MainWindow and outlives the viewport.
    void setLayerStack(LayerStack* layers);
    // Re-read the stack and apply it. Rebuilds the basemap texture only when
    // the raster or relief state actually changed, so toggling an analysis
    // layer never re-fetches tiles.
    void refreshLayers();

    // What the renderer will actually draw for a layer id, read from the
    // overlay objects rather than from the stack. Two unlinked places decide
    // this - the stack and the renderer - so a test that reads the stack back
    // could not catch them drifting apart.
    bool isLayerVisible(const QString& id) const;
    void resetView() { m_camera = m_defaultCamera; update(); }
    void topView() { m_camera.orbit(0.0f, 89.0f - m_camera.elevation); update(); }
    void setElevationCredit(const QString& credit) {
        m_elevationCredit = credit;
        updateMapHud();
    }
    void setPlacementMode(bool active, bool jammer = false);

    // Ruler mode: two terrain clicks report ground distance and bearing.
    // Mutually exclusive with placement, since both consume a terrain click.
    void setMeasureMode(bool active);
    bool measureMode() const { return m_measureActive; }
    void clearMeasurement();
    // Empty until two points have been taken.
    QString measurementSummary() const { return m_measurementSummary; }

    // Vegetation drawing and editing: one tool with two kinds. While it is
    // active a left drag over terrain outlines a stand or clearing, a click
    // selects an area, and dragging a selected area's middle or a corner moves
    // or resizes it. The viewport never changes areas itself - it reports each
    // finished gesture and MainWindow commits it through the undo stack, so
    // every change has one path. Mutually exclusive with measuring and
    // placement. Inside the tool a right drag orbits, since the left button
    // draws.
    void setVegetationDrawMode(bool active, VegetationKind kind = VegetationKind::Stand);
    bool vegetationDrawMode() const { return m_vegetationToolActive; }
    VegetationKind vegetationDrawKind() const { return m_vegetationKind; }
    // A mirror of MainWindow's selection, used to draw it and to find its grips.
    void setVegetationSelection(int index);
    int vegetationSelection() const { return m_vegetationSelection; }
    // Re-reads the engine's areas and cancels any gesture in progress. That is
    // how an undo arriving in the middle of a drag leaves nothing half-applied.
    void refreshVegetationAreas();
    void cancelVegetationGesture();
    bool vegetationGestureActive() const {
        return m_vegGesture.kind != VegetationGestureKind::None;
    }
    // CPU-side state, valid without a rendered frame, for tests and diagnostics.
    // The number of outline vertices the current visibility rules would draw.
    int vegetationOutlineVertexCount() const;
    // Whether a coverage field is on screen, read from the overlay rather than
    // the legend, so a stale texture cannot hide behind a hidden legend.
    bool coverageFieldDrawn() const { return m_heatmap.visible && m_heatmap.hasField(); }
    // Whether the map texture has been rebuilt since the canopy grid last changed.
    bool vegetationWashUpToDate() const;
    void setAnalysisPoint(int sourceIndex, const glm::vec3& receiverPosition);
    void clearAnalysisPoint();
    void setScenarioComparison(
        const std::vector<std::int8_t>& serviceTransition,
        bool visible = true);
    void setScenarioComparisonVisible(bool visible);
    void clearScenarioComparison();
    void setBasemapLoadCallbacks(BasemapOverlay::ProgressCallback progress,
                                 BasemapOverlay::CancelCallback cancel) {
        m_basemap.setLoadCallbacks(std::move(progress), std::move(cancel));
    }
    bool basemapReady() const { return m_basemap.hasTexture(); }
    QString basemapError() const { return m_basemap.lastError(); }
    void setHeatmapMetric(HeatmapOverlay::Metric metric) {
        makeCurrent();
        m_heatmap.update(m_engine->fieldGrid(), metric);
        doneCurrent();
        if (m_heatmapLegend) m_heatmapLegend->setMetric(metric);
        update();
    }

signals:
    void terrainClicked(float worldX, float worldZ);
    void antennaClicked(int index);
    void antennaMoveRequested(int index, float worldX, float worldZ);
    void terrainInspected(float worldX, float worldZ);
    void basemapInfoChanged(QString provider, QString attribution, QString error);
    void placementCancelled();
    void measurementChanged(QString summary);
    void measureModeChanged(bool active);
    void vegetationDrawModeChanged(bool active, int kind);
    void vegetationAreaDrawn(float minX, float minZ, float maxX, float maxZ);
    void vegetationAreaReshaped(int index, float minX, float minZ, float maxX, float maxZ,
                                bool resized);
    // -1 for a click on empty ground.
    void vegetationAreaClicked(int index);
    void vegetationDeleteRequested();
    void vegetationEditRequested();
    // A finished gesture that was deliberately not committed, with the reason.
    void vegetationGestureRejected(QString reason);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    // The readout must not linger over a map the cursor has left.
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    // A modal dialog can take the button release; a gesture must not survive it.
    void focusOutEvent(QFocusEvent*) override;

private:
    SimEngine* m_engine;
    Camera m_camera;
    Camera m_defaultCamera;
    TerrainMesh m_terrain;
    BasemapOverlay m_basemap;
    HeatmapOverlay m_heatmap;
    HeatmapOverlay m_comparisonHeatmap;
    LOSRayOverlay m_losRay;
    AntennaLobeOverlay m_antennaLobe;
    JammerBubbleOverlay m_jammerBubble;
    FresnelOverlay m_fresnel;
    QPoint m_lastMouse;
    QPoint m_pressPos;
    bool m_dragging = false;
    bool m_cameraDragging = false;
    int m_markerPressIndex = -1;
    HeatmapLegend* m_heatmapLegend = nullptr;
    QLabel* m_mapInfoLabel = nullptr;
    QLabel* m_attributionLabel = nullptr;
    QString m_elevationCredit = QStringLiteral("Synthetic elevation · Demo only");
    LayerStack* m_layers = nullptr;
    QString m_rasterSignature;
    void applyRasterLayers(bool force);
    void applyRasterLayersOnce(bool force);
    // Basemap builds can run nested event loops that deliver input and timers;
    // a call arriving during one is coalesced into a single follow-up pass.
    bool m_rasterBuildActive = false;
    bool m_rasterBuildPending = false;
    bool m_rasterBuildForce = false;
    std::uint64_t m_appliedCanopyRevision = 0;
    QStringList m_activeRasterIds;
    MapScaleBar* m_scaleBar = nullptr;
    NorthIndicator* m_northIndicator = nullptr;
    void updateMapFurniture();
    QLabel* m_placementHint = nullptr;
    QLabel* m_comparisonLegend = nullptr;
    bool m_placementActive = false;
    bool m_measureActive = false;
    bool m_measureHasFirst = false;
    float m_measureFirstX = 0.0f, m_measureFirstZ = 0.0f;
    QString m_measurementSummary;
    void takeMeasurementPoint(float worldX, float worldZ);
    // Live readout of whatever the cursor is over. Empty when the cursor is
    // off the terrain, so it never reports a stale position.
    QLabel* m_cursorReadout = nullptr;
    void updateCursorReadout(const QPoint& screenPos);
    void hideCursorReadout();
    bool m_placingJammer = false;
    glm::mat4 m_lastMVP{1.0f};
    bool m_analysisPointActive = false;
    int m_analysisSourceIndex = -1;
    glm::vec3 m_analysisReceiver{0.0f};
    std::vector<std::int8_t> m_serviceTransition;

    static constexpr float kVertScale = 2.5f;

    void drawAntennaMarkers();
    void buildMarkers();
    int antennaAtScreenPosition(const QPoint& screenPos, float radiusPx = 16.0f) const;
    bool raycastTerrain(const QPoint& screenPos,
                        float& outWorldX, float& outWorldZ) const;
    void updateMapHud();
    void repositionHud();

    GLuint m_markerVAO = 0;
    GLuint m_markerVBO = 0;
    int m_markerCount = 0;
    ShaderProgram m_markerShader;

    // --- Drawn vegetation --------------------------------------------------
    enum class VegetationGestureKind { None, Draw, Move, Resize };
    struct VegetationGesture {
        VegetationGestureKind kind = VegetationGestureKind::None;
        VegetationGeometry::Grip grip = VegetationGeometry::Grip::None;
        int index = -1;                           // the area being moved or resized
        glm::vec3 anchor{0.0f};                   // terrain point under the press
        VegetationGeometry::Rect original{};
        VegetationGeometry::Rect preview{};
        glm::vec2 lastGood{0.0f};
        bool lastPrecise = false;
        bool dragged = false;
        // The canopy grid's revision at the press. A release after it changed -
        // an undo, a load, an import - commits nothing.
        std::uint64_t pressCanopyRevision = 0;
        QPoint lastCursor;
    };
    VegetationGesture m_vegGesture;
    // Set when a gesture is cancelled with the button still held, so the rest of
    // that drag neither orbits the camera nor lands as a click.
    bool m_vegSwallowUntilRelease = false;
    bool m_vegetationToolActive = false;
    VegetationKind m_vegetationKind = VegetationKind::Stand;
    int m_vegetationSelection = -1;
    VegetationAreaOverlay m_vegetationOverlay;
    // Built on the CPU, uploaded in paintGL where the context is current.
    std::vector<VegetationGeometry::LineVertex> m_vegCommittedVerts;
    std::vector<VegetationGeometry::LineVertex> m_vegSelectionVerts;
    std::vector<VegetationGeometry::LineVertex> m_vegPreviewVerts;
    bool m_vegCommittedDirty = true;
    bool m_vegSelectionDirty = true;
    bool m_vegPreviewDirty = true;

    glm::mat4 renderMvp() const;
    bool vegetationOutlinesVisible() const;
    void rebuildVegetationCommitted();
    void rebuildVegetationSelection();
    VegetationGeometry::Grip vegetationGripAt(const QPoint& cursor, bool haveWorldHit,
                                              glm::vec2 worldHit) const;
    void updateVegetationHoverCursor(const QPoint& cursor);
    void beginVegetationGesture(const QPoint& cursor);
    bool resolveVegetationPoint(VegetationGesture& gesture, const QPoint& cursor,
                                glm::vec2& out, bool& precise) const;
    void updateVegetationPreview(const QPoint& cursor);
    void finishVegetationGesture(const QPoint& cursor);
    void drawVegetationAreas();
};
