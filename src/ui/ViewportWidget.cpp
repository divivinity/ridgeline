#include "ViewportWidget.h"
#include "TerrainPresentation.h"
#include "map/CanopyShade.h"
#include "map/Hillshade.h"
#include "map/MapMeasure.h"
#include "map/VegetationGeometry.h"
#include "render/ScreenProjection.h"
#include "render/TerrainRaycast.h"
#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLabel>
#include <QCursor>
#include <QFocusEvent>
#include <QScopeGuard>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <limits>
#include <utility>
#include <vector>

ViewportWidget::ViewportWidget(SimEngine* engine, QWidget* parent)
    : QOpenGLWidget(parent), m_engine(engine)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    m_basemap.setPrepareOpenGLCallback([this]() { makeCurrent(); });

    m_heatmapLegend = new HeatmapLegend(this);
    // The coverage toggle may be on before any result exists. Don't imply a
    // computed field by displaying its scale on an unsolved scene.
    m_heatmapLegend->hide();
    m_comparisonHeatmap.visible = false;
    m_losRay.visible = false;
    m_antennaLobe.visible = false;
    m_jammerBubble.visible = false;
    m_fresnel.visible = false;

    m_scaleBar = new MapScaleBar(this);
    m_northIndicator = new NorthIndicator(this);
    m_cursorReadout = new QLabel(this);
    m_cursorReadout->setObjectName("CursorReadout");
    m_cursorReadout->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_cursorReadout->setStyleSheet(
        "QLabel { background: rgba(7, 14, 25, 205); border: 1px solid rgba(148,163,184,35);"
        " border-radius: 7px; color: #dbeafe; padding: 6px 10px; font-size: 12px; }");
    m_cursorReadout->hide();
    m_mapInfoLabel = new QLabel(this);
    m_mapInfoLabel->setObjectName("MapTerrainSummary");
    m_mapInfoLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_mapInfoLabel->setStyleSheet(
        "QLabel { background: rgba(7, 14, 25, 205); border: 1px solid rgba(148,163,184,35);"
        " border-radius: 9px; color: #dbeafe; padding: 9px 12px; font-size: 11px; }"
    );
    m_attributionLabel = new QLabel(this);
    m_attributionLabel->setObjectName("ElevationAttribution");
    m_attributionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_attributionLabel->setStyleSheet(
        "QLabel { background: rgba(7, 14, 25, 185); border-radius: 5px;"
        " color: #cbd5e1; padding: 4px 7px; font-size: 9px; }"
    );
    m_placementHint = new QLabel(this);
    m_placementHint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_placementHint->setAlignment(Qt::AlignCenter);
    m_placementHint->setStyleSheet(
        "QLabel { background: rgba(15,118,110,225); border: 1px solid #2dd4bf;"
        " border-radius: 9px; color: white; padding: 8px 16px; font-weight: 650; }"
    );
    m_placementHint->hide();

    m_comparisonLegend = new QLabel(this);
    m_comparisonLegend->setObjectName("ScenarioComparisonLegend");
    m_comparisonLegend->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_comparisonLegend->setText(
        "<b>SCENARIO CHANGES</b><br>"
        "<span style='color:#22e178'>●</span>&nbsp; Gained service<br>"
        "<span style='color:#f3292e'>●</span>&nbsp; Lost service");
    m_comparisonLegend->setStyleSheet(
        "QLabel { background: rgba(7, 14, 25, 220); border: 1px solid rgba(148,163,184,45);"
        " border-radius: 9px; color: #dbeafe; padding: 9px 12px; font-size: 10px; }"
    );
    m_comparisonLegend->adjustSize();
    m_comparisonLegend->hide();
}

void ViewportWidget::initializeGL() {
    initializeOpenGLFunctions();

    // Preserve the user's comparison-overlay choice when Qt recreates the
    // OpenGL context. The GL texture itself must be uploaded again below.
    const bool comparisonRequested = m_comparisonHeatmap.visible;

    // Destroy GL objects before the context dies, not in the destructor.
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this]() {
        makeCurrent();
        m_terrain.destroy();
        m_basemap.destroy();
        m_heatmap.destroy();
        m_comparisonHeatmap.destroy();
        m_losRay.destroy();
        m_antennaLobe.destroy();
        m_jammerBubble.destroy();
        m_fresnel.destroy();
        m_vegetationOverlay.destroy();
        m_vegGesture = VegetationGesture{};
        if (m_markerVAO) { glDeleteVertexArrays(1, &m_markerVAO); m_markerVAO = 0; }
        if (m_markerVBO) { glDeleteBuffers(1, &m_markerVBO);      m_markerVBO = 0; }
        doneCurrent();
    });

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.08f, 0.12f, 1.0f);

    const auto& terrain = m_engine->terrain();
    m_terrain.build(terrain);
    m_basemap.build(terrain);
    m_heatmap.build(terrain);
    m_comparisonHeatmap.build(terrain);

    const FieldGrid& field = m_engine->fieldGrid();
    if (field.valid())
        m_heatmap.update(field, m_heatmap.metric());
    m_heatmapLegend->setVisible(m_heatmap.visible && field.valid());

    const bool comparisonValid =
        field.valid() && m_serviceTransition.size() == field.cellCount();
    if (comparisonValid)
        m_comparisonHeatmap.updateComparison(field, m_serviceTransition);
    else
        m_serviceTransition.clear();
    m_comparisonHeatmap.visible = comparisonRequested && comparisonValid;
    if (m_comparisonLegend)
        m_comparisonLegend->setVisible(m_comparisonHeatmap.visible);
    m_losRay.build();
    m_antennaLobe.build();
    m_jammerBubble.build();
    m_fresnel.build();
    m_vegetationOverlay.build();
    rebuildVegetationCommitted();
    rebuildVegetationSelection();
    m_vegPreviewDirty = true;

    // Build marker GL objects
    glGenVertexArrays(1, &m_markerVAO);
    glGenBuffers(1, &m_markerVBO);
    m_markerShader.load("assets/shaders/marker.vert", "assets/shaders/marker.frag");
    buildMarkers();

    // Center camera on terrain (target Y in scaled world-space)
    float cx = terrain.worldSize().x * 0.5f;
    float cz = terrain.worldSize().z * 0.5f;
    m_camera.target  = { cx, terrain.heightAtWorld(cx, cz) * kVertScale, cz };
    m_camera.radius  = terrain.worldSize().x * 0.8f;
    m_camera.elevation = 35.0f;
    m_defaultCamera = m_camera;   // save for R key
    // The stack may have been bound before this context existed.
    applyRasterLayers(true);
    updateMapHud();
}

void ViewportWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    m_heatmapLegend->reposition(QSize(w, h));
    if (m_scaleBar)
        m_scaleBar->move(14, h - m_scaleBar->height() - 14);
    if (m_northIndicator)
        m_northIndicator->move(w - m_northIndicator->width() - 14, 14);
    updateMapFurniture();
    repositionHud();
}

void ViewportWidget::paintGL() {
    if (height() == 0) return;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    float aspect = static_cast<float>(width()) / height();
    // Apply vertical exaggeration: scale Y only, in world space before view/proj.
    const glm::mat4 vertScale = glm::scale(glm::mat4(1.0f),
                                            glm::vec3(1.0f, kVertScale, 1.0f));
    m_lastMVP = m_camera.mvp(aspect) * vertScale;
    updateMapFurniture();

    m_terrain.draw(m_lastMVP);
    // Base rasters and relief are composited into the basemap texture, so they
    // draw once here rather than as separate coplanar passes.
    if (m_basemap.visible) m_basemap.draw(m_lastMVP);

    if (m_layers) {
        // Analysis layers draw in stack order, so reordering the panel
        // reorders what covers what.
        for (std::size_t i = 0; i < m_layers->size(); ++i) {
            const MapLayer& layer = m_layers->at(i);
            if (layer.kind != LayerKind::Analysis || !layer.visible) continue;
            if (layer.id == QLatin1String("coverage"))        m_heatmap.draw(m_lastMVP);
            else if (layer.id == QLatin1String("comparison")) m_comparisonHeatmap.draw(m_lastMVP);
            else if (layer.id == QLatin1String("los"))        m_losRay.draw(m_lastMVP);
            else if (layer.id == QLatin1String("pattern"))    m_antennaLobe.draw(m_lastMVP);
            else if (layer.id == QLatin1String("jamming"))    m_jammerBubble.draw(m_lastMVP);
            else if (layer.id == QLatin1String("fresnel"))    m_fresnel.draw(m_lastMVP);
        }
    } else {
        if (m_heatmap.visible)      m_heatmap.draw(m_lastMVP);
        if (m_comparisonHeatmap.visible) m_comparisonHeatmap.draw(m_lastMVP);
        if (m_losRay.visible)       m_losRay.draw(m_lastMVP);
        if (m_antennaLobe.visible)  m_antennaLobe.draw(m_lastMVP);
        if (m_jammerBubble.visible) m_jammerBubble.draw(m_lastMVP);
        if (m_fresnel.visible)      m_fresnel.draw(m_lastMVP);
    }

    // The inspected-point path belongs to the result inspector, not the LOS
    // layer, so it survives that layer being hidden.
    m_losRay.drawProbe(m_lastMVP);
    // After the coverage drape, which blends without writing depth and would
    // otherwise wash over the outlines; before the markers, which stay on top.
    drawVegetationAreas();
    drawAntennaMarkers();
}

void ViewportWidget::buildMarkers() {
    const auto& antennas = m_engine->antennas();
    m_markerCount = static_cast<int>(antennas.size());
    if (m_markerVAO == 0) return;  // GL not ready

    // Pack: x, y, z, r, g, b  per marker
    struct MarkerVert { float x, y, z, r, g, b; };
    std::vector<MarkerVert> verts;
    verts.reserve(antennas.size() + (m_analysisPointActive ? 1 : 0));
    for (const auto& ant : antennas) {
        glm::vec3 col = ant.isJammer ? glm::vec3(1.0f, 0.2f, 0.2f)   // red
                                     : glm::vec3(0.0f, 0.9f, 1.0f);   // cyan
        verts.push_back({ ant.position.x, ant.position.y, ant.position.z,
                          col.r, col.g, col.b });
    }
    if (m_analysisPointActive) {
        verts.push_back({m_analysisReceiver.x, m_analysisReceiver.y,
                         m_analysisReceiver.z, 1.0f, 0.78f, 0.12f});
        m_markerCount = static_cast<int>(antennas.size()) + 1;
    }

    glBindVertexArray(m_markerVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_markerVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(MarkerVert)),
                 verts.empty() ? nullptr : verts.data(),
                 GL_DYNAMIC_DRAW);
    // Position: location 0, 3 floats
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MarkerVert), (void*)0);
    glEnableVertexAttribArray(0);
    // Color: location 1, 3 floats
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MarkerVert),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void ViewportWidget::drawAntennaMarkers() {
    if (m_markerVAO == 0 || m_markerCount == 0) return;
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Disable depth test so markers always show on top
    glDisable(GL_DEPTH_TEST);
    m_markerShader.bind();
    m_markerShader.setMat4("uMVP", m_lastMVP);
    glBindVertexArray(m_markerVAO);
    glDrawArrays(GL_POINTS, 0, m_markerCount);
    glBindVertexArray(0);
    m_markerShader.release();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_PROGRAM_POINT_SIZE);
}

void ViewportWidget::refreshOverlays() {
    m_heatmapLegend->setVisible(m_heatmap.visible && m_engine->fieldGrid().valid());
    makeCurrent();
    m_heatmap.update(m_engine->fieldGrid(), m_heatmap.metric());
    m_losRay.update(m_engine->antennas(), m_engine->terrain());
    m_losRay.updateProbe(m_engine->antennas(), m_engine->terrain(),
                         m_analysisSourceIndex,
                         m_analysisPointActive ? &m_analysisReceiver : nullptr);
    m_antennaLobe.update(m_engine->antennas());
    m_jammerBubble.update(m_engine->antennas());
    m_fresnel.update(m_engine->antennas(), m_engine->terrain());
    buildMarkers();
    doneCurrent();
    update();
}


// Ground metres per screen pixel at the camera's focus. The projection is a
// 45-degree vertical perspective, so the visible height at the target distance
// is 2 * radius * tan(22.5 degrees). Vertical exaggeration scales Y only and
// therefore does not affect horizontal scale.
void ViewportWidget::updateMapFurniture() {
    if (!m_scaleBar && !m_northIndicator) return;
    if (m_northIndicator) m_northIndicator->setCameraAzimuth(m_camera.azimuth);
    if (!m_scaleBar) return;
    if (height() <= 0 || !(m_camera.radius > 0.0f)) {
        m_scaleBar->setMetresPerPixel(0.0);
        return;
    }
    const double visibleHeightM =
        2.0 * static_cast<double>(m_camera.radius) * std::tan(45.0 * 0.5 * 3.14159265358979323846 / 180.0);
    m_scaleBar->setMetresPerPixel(visibleHeightM / static_cast<double>(height()));
}

void ViewportWidget::reloadTerrain() {
    m_heatmapLegend->hide();
    m_analysisPointActive = false;
    m_analysisSourceIndex = -1;
    makeCurrent();
    m_terrain.destroy();
    m_basemap.destroy();
    m_heatmap.destroy();
    m_comparisonHeatmap.destroy();
    m_losRay.destroy();
    m_antennaLobe.destroy();
    m_jammerBubble.destroy();
    m_fresnel.destroy();
    m_vegetationOverlay.destroy();
    const auto& terrain = m_engine->terrain();
    m_terrain.build(terrain);
    m_basemap.build(terrain);
    m_heatmap.build(terrain);
    m_comparisonHeatmap.build(terrain);
    m_serviceTransition.clear();
    m_comparisonHeatmap.visible = false;
    if (m_comparisonLegend) m_comparisonLegend->hide();
    m_losRay.build();
    m_antennaLobe.build();
    m_jammerBubble.build();
    m_fresnel.build();
    m_vegetationOverlay.build();
    // Outlines are draped on the terrain, so new terrain means new outlines.
    cancelVegetationGesture();
    rebuildVegetationCommitted();
    rebuildVegetationSelection();
    m_vegPreviewDirty = true;
    buildMarkers();
    float cx = terrain.worldSize().x * 0.5f;
    float cz = terrain.worldSize().z * 0.5f;
    m_camera.target    = { cx, terrain.heightAtWorld(cx, cz) * kVertScale, cz };
    m_camera.radius    = terrain.worldSize().x * 0.8f;
    m_camera.elevation = 35.0f;
    m_defaultCamera    = m_camera;   // save for R key
    updateMapHud();
    doneCurrent();
    // New terrain means new tiles and a new hillshade, so the cached signature
    // no longer describes what is on screen.
    m_rasterSignature.clear();
    applyRasterLayers(true);
    update();
}

void ViewportWidget::mousePressEvent(QMouseEvent* e) {
    // A new left press means the release owed to a cancelled drag went somewhere
    // else, typically to a modal dialog. Another button pressed while that left
    // button is still down changes nothing: the rest of the drag stays swallowed
    // rather than turning into an orbit.
    if (e->button() == Qt::LeftButton) m_vegSwallowUntilRelease = false;
    if (m_vegGesture.kind != VegetationGestureKind::None && e->button() != Qt::LeftButton) {
        // A second button mid-drag abandons the gesture rather than mixing it
        // with a camera move.
        cancelVegetationGesture();
        return;
    }
    m_pressPos  = e->pos();
    m_lastMouse = e->pos();
    m_dragging  = true;
    m_cameraDragging = false;
    // Inside the vegetation tool the left button belongs to the tool, so a
    // corner placed near a mast does not grab the mast.
    m_markerPressIndex = (e->button() == Qt::LeftButton && !m_placementActive &&
                          !m_vegetationToolActive)
        ? antennaAtScreenPosition(e->pos()) : -1;
    if (m_markerPressIndex >= 0) setCursor(Qt::ClosedHandCursor);
    // Only the left button on its own starts a gesture. With the right or
    // middle button already down the press is part of a camera move; a gesture
    // started there would lose its drag state when that button let go, freezing
    // the preview while the left release still committed.
    if (e->button() == Qt::LeftButton && m_vegetationToolActive &&
        !(e->buttons() & (Qt::RightButton | Qt::MiddleButton)))
        beginVegetationGesture(e->pos());
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_vegSwallowUntilRelease) {
        // The end of a drag whose gesture was cancelled: not a click, not a commit.
        m_vegSwallowUntilRelease = false;
        m_dragging = false;
        m_cameraDragging = false;
        m_markerPressIndex = -1;
        return;
    }
    if (e->button() == Qt::LeftButton && m_vegetationToolActive) {
        finishVegetationGesture(e->pos());
        return;
    }
    if (e->button() == Qt::LeftButton) {
        m_dragging = false;
        const QPoint delta = e->pos() - m_pressPos;
        if (!m_placementActive && m_markerPressIndex >= 0) {
            const int antennaIndex = m_markerPressIndex;
            m_markerPressIndex = -1;
            if (delta.manhattanLength() < QApplication::startDragDistance()) {
                emit antennaClicked(antennaIndex);
            } else {
                float wx = 0.0f, wz = 0.0f;
                if (raycastTerrain(e->pos(), wx, wz))
                    emit antennaMoveRequested(antennaIndex, wx, wz);
            }
            m_cameraDragging = false;
            setCursor(antennaAtScreenPosition(e->pos()) >= 0
                ? Qt::PointingHandCursor : Qt::ArrowCursor);
            return;
        }
        if (!m_cameraDragging &&
            delta.manhattanLength() < QApplication::startDragDistance()) {
            if (m_measureActive) {
                float wx = 0.0f, wz = 0.0f;
                if (raycastTerrain(e->pos(), wx, wz))
                    takeMeasurementPoint(wx, wz);
            } else if (m_placementActive) {
                float wx = 0.0f, wz = 0.0f;
                if (raycastTerrain(e->pos(), wx, wz))
                    emit terrainClicked(wx, wz);
            } else {
                const int antennaIndex = antennaAtScreenPosition(e->pos());
                if (antennaIndex >= 0) {
                    emit antennaClicked(antennaIndex);
                } else {
                    float wx = 0.0f, wz = 0.0f;
                    if (raycastTerrain(e->pos(), wx, wz))
                        emit terrainInspected(wx, wz);
                }
            }
        }
    } else {
        m_dragging = false;
    }
    m_markerPressIndex = -1;
    m_cameraDragging = false;
}

int ViewportWidget::antennaAtScreenPosition(const QPoint& screenPos,
                                             float radiusPx) const {
    if (!m_engine || width() <= 0 || height() <= 0) return -1;
    const auto& antennas = m_engine->antennas();
    if (antennas.empty()) return -1;

    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    const glm::mat4 vertScale = glm::scale(glm::mat4(1.0f),
                                            glm::vec3(1.0f, kVertScale, 1.0f));
    const glm::mat4 mvp = m_camera.mvp(aspect) * vertScale;

    const float radiusSq = radiusPx * radiusPx;
    float bestDistanceSq = radiusSq;
    int bestIndex = -1;

    for (int i = 0; i < static_cast<int>(antennas.size()); ++i) {
        // The projection vegetation grips use too, so a marker and a corner at
        // the same spot are found at the same pixel.
        glm::vec2 px{0.0f};
        if (!ScreenProjection::toScreen(mvp, width(), height(), antennas[i].position, px))
            continue;
        const float dx = px.x - static_cast<float>(screenPos.x());
        const float dy = px.y - static_cast<float>(screenPos.y());
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq <= bestDistanceSq) {
            bestDistanceSq = distanceSq;
            bestIndex = i;
        }
    }
    return bestIndex;
}



void ViewportWidget::setMeasureMode(bool active) {
    if (m_measureActive == active) return;
    m_measureActive = active;
    // Placement and measuring both consume a terrain click, so one cancels the
    // other rather than leaving the next click ambiguous.
    if (active && m_placementActive) setPlacementMode(false);
    if (active && m_vegetationToolActive) setVegetationDrawMode(false);
    if (!active) clearMeasurement();
    setCursor(active ? Qt::CrossCursor : Qt::ArrowCursor);
    emit measureModeChanged(m_measureActive);
    update();
}

void ViewportWidget::clearMeasurement() {
    m_measureHasFirst = false;
    if (!m_measurementSummary.isEmpty()) {
        m_measurementSummary.clear();
        emit measurementChanged(m_measurementSummary);
    }
    m_losRay.clearProbe();
    update();
}

void ViewportWidget::takeMeasurementPoint(float worldX, float worldZ) {
    if (!m_measureHasFirst) {
        m_measureFirstX = worldX;
        m_measureFirstZ = worldZ;
        m_measureHasFirst = true;
        m_measurementSummary = QStringLiteral("Measuring from here - click a second point");
        emit measurementChanged(m_measurementSummary);
        update();
        return;
    }

    const TerrainModel& terrain = m_engine->terrain();
    double metres = 0.0;
    QString bearingText;
    if (terrain.hasGeoReference()) {
        // Geodesic distance, so a long path is not shortened by the flat
        // projection the terrain grid is stored in.
        const auto from = terrain.worldToLatLon(m_measureFirstX, m_measureFirstZ);
        const auto to = terrain.worldToLatLon(worldX, worldZ);
        metres = MapMeasure::haversineMetres(from.latDeg, from.lonDeg, to.latDeg, to.lonDeg);
        const double bearing = MapMeasure::initialBearingDeg(from.latDeg, from.lonDeg,
                                                            to.latDeg, to.lonDeg);
        bearingText = QStringLiteral("%1° %2")
            .arg(bearing, 0, 'f', 1)
            .arg(MapMeasure::compassPoint(bearing));
    } else {
        // No georeference means no true bearing; report the grid distance and
        // say nothing about north rather than inventing one.
        metres = MapMeasure::planarMetres(m_measureFirstX, m_measureFirstZ, worldX, worldZ);
    }

    const float groundFrom = terrain.heightAtWorld(m_measureFirstX, m_measureFirstZ);
    const float groundTo = terrain.heightAtWorld(worldX, worldZ);
    m_measurementSummary = MapMeasure::formatDistance(metres);
    if (!bearingText.isEmpty())
        m_measurementSummary += QStringLiteral("  ·  ") + bearingText;
    m_measurementSummary += QStringLiteral("  ·  %1 m rise")
        .arg(MapMeasure::withoutNegativeZero(static_cast<double>(groundTo - groundFrom), 1),
             0, 'f', 1);

    // Reuse the inspector's probe line to draw the measured span.
    m_losRay.setProbeSpan({m_measureFirstX, groundFrom, m_measureFirstZ},
                          {worldX, groundTo, worldZ});
    m_measureHasFirst = false;
    emit measurementChanged(m_measurementSummary);
    update();
}

void ViewportWidget::setPlacementMode(bool active, bool jammer) {
    if (active && m_vegetationToolActive) setVegetationDrawMode(false);
    m_placementActive = active;
    m_placingJammer = jammer;
    if (active) {
        setCursor(Qt::CrossCursor);
        m_placementHint->setText(jammer
            ? "Click the terrain to place a jammer"
            : "Click the terrain to place an emitter");
        m_placementHint->adjustSize();
        m_placementHint->show();
    } else {
        unsetCursor();
        m_placementHint->hide();
    }
    repositionHud();
}

void ViewportWidget::updateMapHud() {
    const TerrainModel& terrain = m_engine->terrain();
    QString mapText = "Procedural terrain";
    if (terrain.hasGeoReference()) {
        const auto center = terrain.worldToLatLon(
            terrain.worldSize().x * 0.5f, terrain.worldSize().z * 0.5f);
        mapText = QString("%1°, %2°\n%3  ·  %4 m terrain grid")
            .arg(center.latDeg, 0, 'f', 5)
            .arg(center.lonDeg, 0, 'f', 5)
            .arg(TerrainPresentation::areaSummary(terrain.worldSize().x, terrain.worldSize().z))
            .arg(std::max(terrain.metersPerCellX(), terrain.metersPerCellZ()), 0, 'f', 1);
    }
    m_mapInfoLabel->setText(mapText);
    m_mapInfoLabel->adjustSize();

    QString attribution = m_elevationCredit;
    if (terrain.hasGeoReference() && !m_basemap.hasTexture()) {
        attribution.prepend(QStringLiteral("Imagery unavailable  ·  "));
    } else if (terrain.hasGeoReference() && !m_basemap.attribution().isEmpty()) {
        attribution.prepend(m_basemap.attribution() + QStringLiteral("  ·  "));
    }
    m_attributionLabel->setText(attribution);
    m_attributionLabel->setToolTip(m_basemap.lastError());
    m_attributionLabel->adjustSize();
    emit basemapInfoChanged(m_basemap.providerName(),
                            m_basemap.attribution(),
                            m_basemap.lastError());
    repositionHud();
}

void ViewportWidget::repositionHud() {
    const int margin = 14;
    if (m_mapInfoLabel) m_mapInfoLabel->move(margin, margin);
    if (m_attributionLabel) {
        m_attributionLabel->move(
            std::max(margin, width() - m_attributionLabel->width() - margin),
            std::max(margin, height() - m_attributionLabel->height() - margin));
    }
    if (m_placementHint && m_placementHint->isVisible()) {
        m_placementHint->move(std::max(0, (width() - m_placementHint->width()) / 2), margin);
    }
    if (m_comparisonLegend && m_comparisonLegend->isVisible()) {
        const int top = m_mapInfoLabel
            ? m_mapInfoLabel->geometry().bottom() + 8 : margin;
        m_comparisonLegend->move(margin, top);
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* e) {
    // A move with the button already up means the release was taken elsewhere.
    if (m_vegSwallowUntilRelease && !(e->buttons() & Qt::LeftButton))
        m_vegSwallowUntilRelease = false;
    // Likewise for a drag the tool is tracking without a gesture - the rest of a
    // cancelled one, or a camera move - once no button is down at all. Its drag
    // state goes too, or hover feedback and the readout would stay off until the
    // next press. (A live gesture abandons itself further down.)
    if (m_vegetationToolActive && m_dragging &&
        m_vegGesture.kind == VegetationGestureKind::None &&
        !(e->buttons() & (Qt::LeftButton | Qt::RightButton | Qt::MiddleButton))) {
        m_dragging = false;
        m_cameraDragging = false;
        m_markerPressIndex = -1;
    }
    if (!m_dragging) {
        if (m_vegetationToolActive) {
            updateVegetationHoverCursor(e->pos());
        } else if (!m_placementActive && !m_measureActive) {
            setCursor(antennaAtScreenPosition(e->pos()) >= 0
                ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
        updateCursorReadout(e->pos());
        return;
    }
    // While the camera is being dragged the readout would chase the cursor
    // across a moving scene, which reads as noise.
    hideCursorReadout();

    // The rest of a cancelled drag: no orbit, no preview.
    if (m_vegSwallowUntilRelease) return;
    if (m_vegGesture.kind != VegetationGestureKind::None) {
        if (!(e->buttons() & Qt::LeftButton)) {
            // The release went to something else. Abandon the gesture without
            // swallowing, since there is no release left to swallow.
            m_vegGesture = VegetationGesture{};
            m_vegPreviewVerts.clear();
            m_vegPreviewDirty = true;
            m_dragging = false;
            update();
            return;
        }
        m_vegGesture.lastCursor = e->pos();
        if (!m_vegGesture.dragged) {
            if ((e->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
                return;
            m_vegGesture.dragged = true;
        }
        updateVegetationPreview(e->pos());
        return;
    }

    if (m_markerPressIndex >= 0 && (e->buttons() & Qt::LeftButton)) {
        if ((e->pos() - m_pressPos).manhattanLength() >=
            QApplication::startDragDistance()) {
            setCursor(Qt::ClosedHandCursor);
        }
        return;
    }

    QPoint delta = e->pos() - m_lastMouse;
    if (!m_cameraDragging) {
        if ((e->pos() - m_pressPos).manhattanLength() <
            QApplication::startDragDistance()) {
            return;
        }
        m_cameraDragging = true;
        delta = e->pos() - m_pressPos;
    }
    m_lastMouse = e->pos();
    if (e->buttons() & Qt::LeftButton)
        m_camera.orbit(-delta.x() * 0.4f, delta.y() * 0.4f);
    else if (e->buttons() & Qt::MiddleButton)
        m_camera.pan(delta.x(), delta.y());
    else if ((e->buttons() & Qt::RightButton) && m_vegetationToolActive)
        // The left button draws inside the vegetation tool, so orbit moves here.
        m_camera.orbit(-delta.x() * 0.4f, delta.y() * 0.4f);
    update();
}

void ViewportWidget::wheelEvent(QWheelEvent* e) {
    const float steps = e->angleDelta().y() / 120.0f;
    const float radiusBefore = m_camera.radius;

    // Zoom toward the ground under the cursor rather than the current target,
    // so closing in on a ridge does not slide it out of frame and force a pan.
    float wx = 0.0f, wz = 0.0f;
    const bool overTerrain = raycastTerrain(e->position().toPoint(), wx, wz);

    m_camera.zoom(steps);

    if (overTerrain && radiusBefore > 0.0f) {
        const float shrink = 1.0f - m_camera.radius / radiusBefore;
        if (std::isfinite(shrink) && shrink > 0.0f) {
            const glm::vec3 focus{wx, m_engine->terrain().heightAtWorld(wx, wz) * kVertScale, wz};
            m_camera.target += (focus - m_camera.target) * shrink;
        }
    }
    if (m_vegGesture.kind != VegetationGestureKind::None && m_vegGesture.dragged)
        updateVegetationPreview(m_vegGesture.lastCursor);
    updateCursorReadout(e->position().toPoint());
    update();
}

void ViewportWidget::leaveEvent(QEvent* e) {
    hideCursorReadout();
    QOpenGLWidget::leaveEvent(e);
}

void ViewportWidget::focusOutEvent(QFocusEvent* e) {
    cancelVegetationGesture();
    QOpenGLWidget::focusOutEvent(e);
}

void ViewportWidget::hideCursorReadout() {
    if (m_cursorReadout && m_cursorReadout->isVisible()) m_cursorReadout->hide();
}

void ViewportWidget::updateCursorReadout(const QPoint& screenPos) {
    if (!m_cursorReadout) return;
    float wx = 0.0f, wz = 0.0f;
    if (!raycastTerrain(screenPos, wx, wz)) {
        hideCursorReadout();
        return;
    }
    const TerrainModel& terrain = m_engine->terrain();
    const float ground = terrain.heightAtWorld(wx, wz);
    QString text;
    if (terrain.hasGeoReference()) {
        const auto p = terrain.worldToLatLon(wx, wz);
        text = QStringLiteral("%1°, %2°   %3 m")
                   .arg(p.latDeg, 0, 'f', 5)
                   .arg(p.lonDeg, 0, 'f', 5)
                   .arg(MapMeasure::withoutNegativeZero(static_cast<double>(ground)), 0, 'f', 0);
    } else {
        text = QStringLiteral("X %1 m · Z %2 m   %3 m")
                   .arg(static_cast<double>(wx), 0, 'f', 0)
                   .arg(static_cast<double>(wz), 0, 'f', 0)
                   .arg(MapMeasure::withoutNegativeZero(static_cast<double>(ground)), 0, 'f', 0);
    }
    m_cursorReadout->setText(text);
    m_cursorReadout->adjustSize();
    // Sit just below-right of the cursor, flipped back inside near an edge.
    int x = screenPos.x() + 18;
    int y = screenPos.y() + 20;
    if (x + m_cursorReadout->width() > width()) x = screenPos.x() - m_cursorReadout->width() - 12;
    if (y + m_cursorReadout->height() > height()) y = screenPos.y() - m_cursorReadout->height() - 12;
    m_cursorReadout->move(std::max(0, x), std::max(0, y));
    m_cursorReadout->show();
    m_cursorReadout->raise();
}

void ViewportWidget::setLayerStack(LayerStack* layers) {
    m_layers = layers;
    if (!m_layers) return;
    m_layers->addListener([this] { refreshLayers(); });
    refreshLayers();
}

// A compact description of everything that affects the composited basemap
// texture. Comparing it means an analysis-layer toggle never triggers a tile
// fetch, while a raster or relief change always does.
void ViewportWidget::applyRasterLayers(bool force) {
    // Building the basemap can run nested event loops - tile-cache misses,
    // Google session requests - and those deliver input and timers. A second
    // call arriving from inside one would destroy the basemap in the middle of
    // its own build, so it is coalesced into one follow-up pass instead.
    if (m_rasterBuildActive) {
        m_rasterBuildPending = true;
        m_rasterBuildForce = m_rasterBuildForce || force;
        return;
    }
    m_rasterBuildActive = true;
    const auto done = qScopeGuard([this] { m_rasterBuildActive = false; });
    do {
        applyRasterLayersOnce(force);
        force = std::exchange(m_rasterBuildForce, false);
    } while (std::exchange(m_rasterBuildPending, false));
}

void ViewportWidget::applyRasterLayersOnce(bool force) {
    if (!m_layers) return;

    std::vector<BasemapOverlay::RasterRequest> rasters;
    QString signature;
    for (const MapLayer& layer : m_layers->visibleOfKind(LayerKind::BaseRaster)) {
        rasters.push_back({layer.id, layer.opacity});
        signature += layer.id + QString::number(layer.opacity, 'f', 3) + ";";
    }

    const MapLayer* relief = m_layers->find(QStringLiteral("hillshade"));
    const bool reliefOn = relief && relief->visible;
    if (reliefOn)
        signature += "hillshade" + QString::number(relief->opacity, 'f', 3) + ";";

    const MapLayer* canopy = m_layers->find(QStringLiteral("vegetation"));
    // Vegetation only contributes when there is vegetation to draw, so toggling
    // the layer on an unvegetated scene neither rebuilds the texture nor makes
    // the layer report itself as drawn.
    const bool canopyOn = canopy && canopy->visible &&
                          m_engine && m_engine->terrain().hasVegetation();
    if (canopyOn)
        signature += "vegetation" + QString::number(canopy->opacity, 'f', 3) + "@" +
                     QString::number(m_engine->canopyRevision()) + ";";

    const bool anyRaster = !rasters.empty() || reliefOn;
    m_basemap.visible = anyRaster;
    m_activeRasterIds.clear();
    for (const BasemapOverlay::RasterRequest& raster : rasters)
        m_activeRasterIds << raster.layerId;
    if (reliefOn) m_activeRasterIds << QStringLiteral("hillshade");
    if (canopyOn) m_activeRasterIds << QStringLiteral("vegetation");

    // MainWindow binds the stack during construction, before this widget has a
    // GL context. makeCurrent() and the texture build below both require one,
    // so defer to initializeGL and leave the signature empty to force it.
    if (!isValid()) {
        m_rasterSignature.clear();
        return;
    }
    // From here the texture reflects the current canopy grid: it is either
    // rebuilt below or its signature proves nothing it draws has changed.
    m_appliedCanopyRevision = m_engine ? m_engine->canopyRevision() : 0;
    if (!force && signature == m_rasterSignature) return;
    m_rasterSignature = signature;
    if (!anyRaster) return;

    m_basemap.setRasterLayers(std::move(rasters));
    if (reliefOn) {
        // Computed from the real elevation grid, never the vertically
        // exaggerated display mesh.
        m_basemap.setHillshade(Hillshade::render(m_engine->terrain(), 315.0, 45.0),
                               relief->opacity);
    } else {
        m_basemap.setHillshade(QImage(), 0.0f);
    }

    if (canopyOn)
        m_basemap.setCanopyTint(CanopyShade::render(m_engine->terrain()),
                                canopy->opacity);
    else
        m_basemap.setCanopyTint(QImage(), 0.0f);

    makeCurrent();
    m_basemap.destroy();
    m_basemap.build(m_engine->terrain());
    doneCurrent();
    updateMapHud();
}

void ViewportWidget::refreshLayers() {
    if (!m_layers) return;
    const auto on = [this](const char* id) {
        const MapLayer* layer = m_layers->find(QLatin1String(id));
        return layer && layer->visible;
    };
    m_heatmap.visible          = on("coverage");
    m_comparisonHeatmap.visible = on("comparison") && !m_serviceTransition.empty();
    m_losRay.visible           = on("los");
    m_antennaLobe.visible      = on("pattern");
    m_jammerBubble.visible     = on("jamming");
    m_fresnel.visible          = on("fresnel");
    m_heatmapLegend->setVisible(m_heatmap.visible && m_engine->fieldGrid().valid());
    applyRasterLayers(false);
    update();
}

bool ViewportWidget::isLayerVisible(const QString& id) const {
    if (id == QLatin1String("coverage"))   return m_heatmap.visible;
    if (id == QLatin1String("comparison")) return m_comparisonHeatmap.visible;
    if (id == QLatin1String("los"))        return m_losRay.visible;
    if (id == QLatin1String("pattern"))    return m_antennaLobe.visible;
    if (id == QLatin1String("jamming"))    return m_jammerBubble.visible;
    if (id == QLatin1String("fresnel"))    return m_fresnel.visible;
    // Rasters and relief are composited into one texture, so "drawn" means the
    // basemap is being drawn and this layer contributed to it.
    return m_basemap.visible && m_activeRasterIds.contains(id);
}

void ViewportWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Escape:
            // Innermost first: a drag, then measuring or placement, then the
            // selection, then the vegetation tool itself.
            if (m_vegGesture.kind != VegetationGestureKind::None) {
                cancelVegetationGesture();
            } else if (m_measureActive) {
                setMeasureMode(false);
            } else if (m_placementActive) {
                setPlacementMode(false);
                emit placementCancelled();
            } else if (m_vegetationToolActive && m_vegetationSelection >= 0) {
                emit vegetationAreaClicked(-1);
            } else if (m_vegetationToolActive) {
                setVegetationDrawMode(false);
            }
            break;
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            if (m_vegetationToolActive && m_vegetationSelection >= 0 &&
                m_vegGesture.kind == VegetationGestureKind::None) {
                emit vegetationDeleteRequested();
                break;
            }
            QOpenGLWidget::keyPressEvent(e);
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_vegetationToolActive && m_vegetationSelection >= 0 &&
                m_vegGesture.kind == VegetationGestureKind::None) {
                emit vegetationEditRequested();
                break;
            }
            QOpenGLWidget::keyPressEvent(e);
            return;
        case Qt::Key_T:
            topView();
            break;
        case Qt::Key_R:
            resetView();
            break;
        case Qt::Key_Left:
            m_camera.orbit(-5.0f, 0.0f);
            break;
        case Qt::Key_Right:
            m_camera.orbit( 5.0f, 0.0f);
            break;
        case Qt::Key_Up:
            m_camera.orbit(0.0f, -5.0f);
            break;
        case Qt::Key_Down:
            m_camera.orbit(0.0f,  5.0f);
            break;
        default:
            QOpenGLWidget::keyPressEvent(e);
            return;
    }
    update();
}

bool ViewportWidget::raycastTerrain(const QPoint& screenPos,
                                    float& outWorldX, float& outWorldZ) const {
    if (!m_engine || width() <= 0 || height() <= 0) return false;
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    glm::vec3 terrainPoint{0.0f};
    if (!TerrainRaycast::fromScreen(m_engine->terrain(), m_camera.mvp(aspect),
                                    width(), height(),
                                    static_cast<float>(screenPos.x()),
                                    static_cast<float>(screenPos.y()),
                                    kVertScale, terrainPoint)) {
        return false;
    }
    outWorldX = terrainPoint.x;
    outWorldZ = terrainPoint.z;
    return true;
}

void ViewportWidget::setAnalysisPoint(int sourceIndex,
                                      const glm::vec3& receiverPosition) {
    m_analysisPointActive = true;
    m_analysisSourceIndex = sourceIndex;
    m_analysisReceiver = receiverPosition;
    makeCurrent();
    m_losRay.updateProbe(m_engine->antennas(), m_engine->terrain(),
                         m_analysisSourceIndex, &m_analysisReceiver);
    buildMarkers();
    doneCurrent();
    update();
}

void ViewportWidget::clearAnalysisPoint() {
    if (!m_analysisPointActive) return;
    m_analysisPointActive = false;
    m_analysisSourceIndex = -1;
    makeCurrent();
    m_losRay.updateProbe(m_engine->antennas(), m_engine->terrain(), -1, nullptr);
    buildMarkers();
    doneCurrent();
    update();
}

void ViewportWidget::setScenarioComparison(
    const std::vector<std::int8_t>& serviceTransition,
    bool visible) {
    const FieldGrid& grid = m_engine->fieldGrid();
    const bool valid = grid.valid() &&
        serviceTransition.size() == grid.cellCount();
    m_serviceTransition = valid ? serviceTransition
                                : std::vector<std::int8_t>{};
    makeCurrent();
    m_comparisonHeatmap.updateComparison(
        grid, m_serviceTransition);
    m_comparisonHeatmap.visible = visible && valid;
    doneCurrent();
    if (m_comparisonLegend)
        m_comparisonLegend->setVisible(m_comparisonHeatmap.visible);
    repositionHud();
    update();
}

void ViewportWidget::setScenarioComparisonVisible(bool visible) {
    m_comparisonHeatmap.visible = visible && !m_serviceTransition.empty();
    if (m_comparisonLegend)
        m_comparisonLegend->setVisible(m_comparisonHeatmap.visible);
    repositionHud();
    update();
}

void ViewportWidget::clearScenarioComparison() {
    m_serviceTransition.clear();
    m_comparisonHeatmap.visible = false;
    makeCurrent();
    m_comparisonHeatmap.updateComparison({}, m_serviceTransition);
    doneCurrent();
    if (m_comparisonLegend) m_comparisonLegend->hide();
    update();
}

// ---------------------------------------------------------------------------
// Drawn vegetation
// ---------------------------------------------------------------------------

glm::mat4 ViewportWidget::renderMvp() const {
    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    return m_camera.mvp(aspect) *
           glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, kVertScale, 1.0f));
}

void ViewportWidget::setVegetationDrawMode(bool active, VegetationKind kind) {
    if (!active) {
        if (!m_vegetationToolActive) return;
        cancelVegetationGesture();
        m_vegetationToolActive = false;
        m_vegetationSelection = -1;
        rebuildVegetationSelection();
        unsetCursor();
        emit vegetationDrawModeChanged(false, static_cast<int>(m_vegetationKind));
        update();
        return;
    }

    const bool changed = !m_vegetationToolActive || m_vegetationKind != kind;
    // Measuring and placement both own the left button too, so they end here -
    // placement with its cancellation signal, so MainWindow's copy of that
    // state cannot go stale.
    if (m_measureActive) setMeasureMode(false);
    if (m_placementActive) {
        setPlacementMode(false);
        emit placementCancelled();
    }
    cancelVegetationGesture();
    m_vegetationToolActive = true;
    m_vegetationKind = kind;
    setCursor(Qt::CrossCursor);
    rebuildVegetationSelection();
    if (changed) emit vegetationDrawModeChanged(true, static_cast<int>(kind));
    update();
}

void ViewportWidget::setVegetationSelection(int index) {
    const int count = m_engine ? static_cast<int>(m_engine->vegetation().size()) : 0;
    m_vegetationSelection = (index >= 0 && index < count) ? index : -1;
    rebuildVegetationSelection();
    update();
}

void ViewportWidget::refreshVegetationAreas() {
    cancelVegetationGesture();
    const int count = static_cast<int>(m_engine->vegetation().size());
    if (m_vegetationSelection >= count) m_vegetationSelection = -1;
    rebuildVegetationCommitted();
    rebuildVegetationSelection();
    update();
}

void ViewportWidget::cancelVegetationGesture() {
    if (m_vegGesture.kind == VegetationGestureKind::None) return;
    m_vegSwallowUntilRelease = m_dragging;
    m_vegGesture = VegetationGesture{};
    if (!m_vegPreviewVerts.empty()) {
        m_vegPreviewVerts.clear();
        m_vegPreviewDirty = true;
    }
    update();
}

int ViewportWidget::vegetationOutlineVertexCount() const {
    std::size_t count = 0;
    if (vegetationOutlinesVisible()) count += m_vegCommittedVerts.size();
    if (m_vegetationToolActive) count += m_vegSelectionVerts.size();
    if (m_vegGesture.kind != VegetationGestureKind::None && m_vegGesture.dragged)
        count += m_vegPreviewVerts.size();
    return static_cast<int>(count);
}

bool ViewportWidget::vegetationWashUpToDate() const {
    return m_engine && m_appliedCanopyRevision == m_engine->canopyRevision();
}

bool ViewportWidget::vegetationOutlinesVisible() const {
    // What is being edited must be visible, whatever the layer says.
    if (m_vegetationToolActive) return true;
    if (!m_layers) return true;
    const MapLayer* layer = m_layers->find(QStringLiteral("vegetation"));
    return layer && layer->visible;
}

void ViewportWidget::rebuildVegetationCommitted() {
    m_vegCommittedVerts.clear();
    if (m_engine) {
        const TerrainModel& terrain = m_engine->terrain();
        for (const VegetationArea& area : m_engine->vegetation()) {
            if (!area.valid()) continue;
            VegetationGeometry::appendOutline(
                m_vegCommittedVerts, terrain, VegetationGeometry::boundsOf(area),
                VegetationGeometry::styleFor(area.kind, VegetationGeometry::Role::Committed));
        }
    }
    m_vegCommittedDirty = true;
}

void ViewportWidget::rebuildVegetationSelection() {
    m_vegSelectionVerts.clear();
    if (m_engine && m_vegetationToolActive && m_vegetationSelection >= 0) {
        const auto& areas = m_engine->vegetation();
        if (m_vegetationSelection < static_cast<int>(areas.size())) {
            const VegetationArea& area = areas[static_cast<std::size_t>(m_vegetationSelection)];
            if (area.valid()) {
                const TerrainModel& terrain = m_engine->terrain();
                VegetationGeometry::appendOutline(
                    m_vegSelectionVerts, terrain, VegetationGeometry::boundsOf(area),
                    VegetationGeometry::styleFor(area.kind, VegetationGeometry::Role::Selected));
                // The ground the solver actually paints, which rounds outward
                // from the drawn rectangle. Shown so the two cannot disagree
                // without the operator seeing it.
                const VegetationAreas::Footprint fp = VegetationAreas::footprint(terrain, area);
                if (!fp.empty) {
                    VegetationGeometry::appendOutline(
                        m_vegSelectionVerts, terrain, {fp.minX, fp.minZ, fp.maxX, fp.maxZ},
                        VegetationGeometry::styleFor(area.kind, VegetationGeometry::Role::Footprint));
                }
            }
        }
    }
    m_vegSelectionDirty = true;
}

VegetationGeometry::Grip ViewportWidget::vegetationGripAt(const QPoint& cursor,
                                                          bool haveWorldHit,
                                                          glm::vec2 worldHit) const {
    if (!m_engine) return VegetationGeometry::Grip::None;
    const auto& areas = m_engine->vegetation();
    if (m_vegetationSelection < 0 || m_vegetationSelection >= static_cast<int>(areas.size()))
        return VegetationGeometry::Grip::None;
    const VegetationGeometry::Rect rect =
        VegetationGeometry::boundsOf(areas[static_cast<std::size_t>(m_vegetationSelection)]);
    const auto corners = VegetationGeometry::cornersOnMesh(m_engine->terrain(), rect);
    VegetationGeometry::ProjectedCorners projected;
    const glm::mat4 mvp = renderMvp();
    for (std::size_t i = 0; i < 4; ++i)
        projected.visible[i] = ScreenProjection::toScreen(mvp, width(), height(), corners[i],
                                                          projected.px[i]);
    return VegetationGeometry::hitTest(rect, projected,
                                       {static_cast<float>(cursor.x()),
                                        static_cast<float>(cursor.y())},
                                       haveWorldHit, worldHit);
}

void ViewportWidget::updateVegetationHoverCursor(const QPoint& cursor) {
    const auto& areas = m_engine->vegetation();
    if (m_vegetationSelection >= 0 && m_vegetationSelection < static_cast<int>(areas.size())) {
        float wx = 0.0f, wz = 0.0f;
        const bool hit = raycastTerrain(cursor, wx, wz);
        const VegetationGeometry::Grip grip = vegetationGripAt(cursor, hit, {wx, wz});
        if (grip == VegetationGeometry::Grip::Body) {
            setCursor(Qt::SizeAllCursor);
            return;
        }
        if (grip != VegetationGeometry::Grip::None) {
            // The diagonal resize cursor that points along the grabbed corner,
            // judged on screen, where the rectangle may be rotated.
            const VegetationGeometry::Rect rect =
                VegetationGeometry::boundsOf(areas[static_cast<std::size_t>(m_vegetationSelection)]);
            const auto corners = VegetationGeometry::cornersOnMesh(m_engine->terrain(), rect);
            const glm::mat4 mvp = renderMvp();
            std::array<glm::vec2, 4> px{};
            std::array<bool, 4> onScreen{};
            glm::vec2 centre{0.0f};
            int visible = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                onScreen[i] = ScreenProjection::toScreen(mvp, width(), height(), corners[i], px[i]);
                if (onScreen[i]) {
                    centre += px[i];
                    ++visible;
                }
            }
            const auto corner = static_cast<std::size_t>(grip) -
                                static_cast<std::size_t>(VegetationGeometry::Grip::MinXMinZ);
            if (visible > 0 && corner < 4 && onScreen[corner]) {
                const glm::vec2 offset = px[corner] - centre / static_cast<float>(visible);
                setCursor(offset.x * offset.y > 0.0f ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
            } else {
                setCursor(Qt::SizeAllCursor);
            }
            return;
        }
    }
    setCursor(Qt::CrossCursor);
}

void ViewportWidget::beginVegetationGesture(const QPoint& cursor) {
    float wx = 0.0f, wz = 0.0f;
    // A press on sky starts nothing, so a drag from there orbits as usual.
    if (!raycastTerrain(cursor, wx, wz)) return;

    const TerrainModel& terrain = m_engine->terrain();
    VegetationGesture gesture;
    gesture.anchor = {wx, VegetationGeometry::meshHeightAtWorld(terrain, wx, wz), wz};
    gesture.lastGood = {wx, wz};
    gesture.lastPrecise = true;
    gesture.lastCursor = cursor;
    gesture.pressCanopyRevision = m_engine->canopyRevision();

    const auto& areas = m_engine->vegetation();
    const bool haveSelection =
        m_vegetationSelection >= 0 && m_vegetationSelection < static_cast<int>(areas.size()) &&
        areas[static_cast<std::size_t>(m_vegetationSelection)].valid();
    const VegetationGeometry::Grip grip = haveSelection
        ? vegetationGripAt(cursor, true, {wx, wz}) : VegetationGeometry::Grip::None;

    // A selected corner resizes, the selected body moves, anywhere else draws.
    // So drawing a stand inside the selected one needs a deselect first.
    if (grip == VegetationGeometry::Grip::None) {
        gesture.kind = VegetationGestureKind::Draw;
        gesture.preview = {wx, wz, wx, wz};
    } else {
        gesture.kind = grip == VegetationGeometry::Grip::Body ? VegetationGestureKind::Move
                                                              : VegetationGestureKind::Resize;
        gesture.grip = grip;
        gesture.index = m_vegetationSelection;
        gesture.original =
            VegetationGeometry::boundsOf(areas[static_cast<std::size_t>(m_vegetationSelection)]);
        gesture.preview = gesture.original;
    }
    m_vegGesture = gesture;
}

bool ViewportWidget::resolveVegetationPoint(VegetationGesture& gesture, const QPoint& cursor,
                                            glm::vec2& out, bool& precise) const {
    // Terrain first. Then the horizontal plane through the anchor, so a drag
    // that leaves the terrain keeps the corner under the cursor rather than
    // letting it jump to wherever the ray next meets ground. Then the last good
    // point, which is marked imprecise so a release there commits nothing.
    const glm::vec3 world = m_engine->terrain().worldSize();
    float wx = 0.0f, wz = 0.0f;
    if (raycastTerrain(cursor, wx, wz)) {
        out = {wx, wz};
        precise = true;
        gesture.lastGood = out;
        return true;
    }
    if (width() > 0 && height() > 0) {
        const float aspect = static_cast<float>(width()) / static_cast<float>(height());
        glm::vec2 plane{0.0f};
        if (TerrainRaycast::onHorizontalPlane(m_camera.mvp(aspect), width(), height(),
                                              static_cast<float>(cursor.x()),
                                              static_cast<float>(cursor.y()),
                                              kVertScale, gesture.anchor.y, plane)) {
            out = {std::clamp(plane.x, 0.0f, world.x), std::clamp(plane.y, 0.0f, world.z)};
            precise = true;
            gesture.lastGood = out;
            return true;
        }
    }
    out = gesture.lastGood;
    precise = false;
    return false;
}

void ViewportWidget::updateVegetationPreview(const QPoint& cursor) {
    if (m_vegGesture.kind == VegetationGestureKind::None) return;
    glm::vec2 point{0.0f};
    bool precise = false;
    resolveVegetationPoint(m_vegGesture, cursor, point, precise);
    m_vegGesture.lastPrecise = precise;

    const TerrainModel& terrain = m_engine->terrain();
    const glm::vec3 world = terrain.worldSize();
    const glm::vec2 extent{world.x, world.z};
    const glm::vec2 anchor{m_vegGesture.anchor.x, m_vegGesture.anchor.z};
    m_vegGesture.preview = m_vegGesture.kind == VegetationGestureKind::Draw
        ? VegetationGeometry::fromCorners(anchor, point, extent)
        : VegetationGeometry::reshape(m_vegGesture.original, m_vegGesture.grip, anchor, point, extent);

    VegetationKind kind = m_vegetationKind;
    const auto& areas = m_engine->vegetation();
    if (m_vegGesture.kind != VegetationGestureKind::Draw && m_vegGesture.index >= 0 &&
        m_vegGesture.index < static_cast<int>(areas.size()))
        kind = areas[static_cast<std::size_t>(m_vegGesture.index)].kind;
    m_vegPreviewVerts.clear();
    VegetationGeometry::appendOutline(
        m_vegPreviewVerts, terrain, m_vegGesture.preview,
        VegetationGeometry::styleFor(kind, VegetationGeometry::Role::Preview));
    m_vegPreviewDirty = true;
    update();
}

void ViewportWidget::finishVegetationGesture(const QPoint& cursor) {
    m_dragging = false;
    m_cameraDragging = false;
    m_markerPressIndex = -1;
    // Cleared before anything is emitted, so a handler that re-enters the
    // viewport sees no gesture in progress.
    VegetationGesture gesture = m_vegGesture;
    m_vegGesture = VegetationGesture{};
    if (!m_vegPreviewVerts.empty()) {
        m_vegPreviewVerts.clear();
        m_vegPreviewDirty = true;
    }
    update();

    // The press missed the terrain, so any drag was a camera move.
    if (gesture.kind == VegetationGestureKind::None) return;

    if (!gesture.dragged) {
        emit vegetationAreaClicked(VegetationGeometry::pick(m_engine->vegetation(),
                                                            gesture.anchor.x, gesture.anchor.z));
        return;
    }

    glm::vec2 point{0.0f};
    bool precise = false;
    resolveVegetationPoint(gesture, cursor, point, precise);
    const glm::vec3 world = m_engine->terrain().worldSize();
    const glm::vec2 extent{world.x, world.z};
    const glm::vec2 anchor{gesture.anchor.x, gesture.anchor.z};
    const VegetationGeometry::Rect rect = gesture.kind == VegetationGestureKind::Draw
        ? VegetationGeometry::fromCorners(anchor, point, extent)
        : VegetationGeometry::reshape(gesture.original, gesture.grip, anchor, point, extent);

    if (m_engine->canopyRevision() != gesture.pressCanopyRevision) {
        emit vegetationGestureRejected(
            QStringLiteral("Vegetation changed during the drag; nothing was changed"));
        return;
    }
    if (!precise) {
        emit vegetationGestureRejected(
            QStringLiteral("Released off the map; nothing was changed"));
        return;
    }
    if (gesture.kind == VegetationGestureKind::Draw)
        emit vegetationAreaDrawn(rect.minX, rect.minZ, rect.maxX, rect.maxZ);
    else
        emit vegetationAreaReshaped(gesture.index, rect.minX, rect.minZ, rect.maxX, rect.maxZ,
                                    gesture.kind == VegetationGestureKind::Resize);
}

void ViewportWidget::drawVegetationAreas() {
    if (!m_vegetationOverlay.ready()) return;
    using Slot = VegetationAreaOverlay::Slot;
    if (m_vegCommittedDirty) {
        m_vegetationOverlay.upload(Slot::Committed, m_vegCommittedVerts);
        m_vegCommittedDirty = false;
    }
    if (m_vegSelectionDirty) {
        m_vegetationOverlay.upload(Slot::Selection, m_vegSelectionVerts);
        m_vegSelectionDirty = false;
    }
    if (m_vegPreviewDirty) {
        m_vegetationOverlay.upload(Slot::Preview, m_vegPreviewVerts);
        m_vegPreviewDirty = false;
    }

    VegetationAreaOverlay::DrawParams params;
    params.mvp = m_lastMVP;
    const glm::vec3 eye = m_camera.eye();
    params.eyeUnscaled = {eye.x, eye.y / kVertScale, eye.z};
    const qreal ratio = devicePixelRatioF();
    params.viewportPx = {static_cast<float>(width() * ratio),
                         static_cast<float>(height() * ratio)};
    params.vertScale = kVertScale;
    // Camera::mvp's 45-degree vertical field of view over the framebuffer.
    params.pixelAngleRad = static_cast<float>(45.0 * 3.14159265358979323846 / 180.0) /
                           std::max(params.viewportPx.y, 1.0f);
    // The same ground scale the scale bar uses, so dashes keep their size on
    // screen at every zoom.
    const double visibleHeightM = 2.0 * static_cast<double>(m_camera.radius) *
                                  std::tan(22.5 * 3.14159265358979323846 / 180.0);
    const double metresPerPixel = height() > 0 ? visibleHeightM / static_cast<double>(height()) : 1.0;
    params.dashPeriodM = static_cast<float>(std::max(1.0, 14.0 * metresPerPixel));
    params.committed = vegetationOutlinesVisible();
    params.selection = m_vegetationToolActive;
    params.preview = m_vegGesture.kind != VegetationGestureKind::None && m_vegGesture.dragged;
    m_vegetationOverlay.draw(params);
}
