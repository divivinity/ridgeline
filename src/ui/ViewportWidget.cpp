#include "ViewportWidget.h"
#include "ScenarioTree.h"
#include "render/TerrainRaycast.h"
#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLabel>
#include <QCursor>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

ViewportWidget::ViewportWidget(SimEngine* engine, QWidget* parent)
    : QOpenGLWidget(parent), m_engine(engine)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // MainWindow already owns the authoritative source-selection path. Route
    // map clicks through that same slot so subsequent edits target the source
    // the user clicked rather than whatever happened to be selected before.
    if (parent) {
        QObject::connect(this, SIGNAL(antennaClicked(int)),
                         parent, SLOT(onAntennaSelected(int)));
    }

    m_heatmapLegend = new HeatmapLegend(this);
    // Heatmap starts visible (matches OverlayToolbar default of checked=true)
    m_heatmapLegend->show();
    m_losRay.visible = false;
    m_antennaLobe.visible = false;
    m_jammerBubble.visible = false;
    m_fresnel.visible = false;

    m_mapInfoLabel = new QLabel(this);
    m_mapInfoLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_mapInfoLabel->setStyleSheet(
        "QLabel { background: rgba(7, 14, 25, 205); border: 1px solid rgba(148,163,184,35);"
        " border-radius: 9px; color: #dbeafe; padding: 9px 12px; font-size: 11px; }"
    );
    m_attributionLabel = new QLabel(this);
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
}

void ViewportWidget::initializeGL() {
    initializeOpenGLFunctions();

    // Destroy GL objects before the context dies, not in the destructor.
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this]() {
        makeCurrent();
        m_terrain.destroy();
        m_basemap.destroy();
        m_heatmap.destroy();
        m_losRay.destroy();
        m_antennaLobe.destroy();
        m_jammerBubble.destroy();
        m_fresnel.destroy();
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
    m_losRay.build();
    m_antennaLobe.build();
    m_jammerBubble.build();
    m_fresnel.build();

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
    updateMapHud();
}

void ViewportWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    m_heatmapLegend->reposition(QSize(w, h));
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

    m_terrain.draw(m_lastMVP);
    if (m_basemap.visible)      m_basemap.draw(m_lastMVP);
    if (m_heatmap.visible)      m_heatmap.draw(m_lastMVP);
    if (m_losRay.visible)       m_losRay.draw(m_lastMVP);
    if (m_antennaLobe.visible)  m_antennaLobe.draw(m_lastMVP);
    if (m_jammerBubble.visible) m_jammerBubble.draw(m_lastMVP);
    if (m_fresnel.visible)      m_fresnel.draw(m_lastMVP);
    drawAntennaMarkers();
}

void ViewportWidget::buildMarkers() {
    const auto& antennas = m_engine->antennas();
    m_markerCount = static_cast<int>(antennas.size());
    if (m_markerVAO == 0) return;  // GL not ready

    // Pack: x, y, z, r, g, b  per marker
    struct MarkerVert { float x, y, z, r, g, b; };
    std::vector<MarkerVert> verts;
    verts.reserve(antennas.size());
    for (const auto& ant : antennas) {
        glm::vec3 col = ant.isJammer ? glm::vec3(1.0f, 0.2f, 0.2f)   // red
                                     : glm::vec3(0.0f, 0.9f, 1.0f);   // cyan
        verts.push_back({ ant.position.x, ant.position.y, ant.position.z,
                          col.r, col.g, col.b });
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
    makeCurrent();
    m_heatmap.update(m_engine->fieldGrid());
    m_losRay.update(m_engine->antennas(), m_engine->terrain());
    m_antennaLobe.update(m_engine->antennas());
    m_jammerBubble.update(m_engine->antennas());
    m_fresnel.update(m_engine->antennas(), m_engine->terrain());
    buildMarkers();
    doneCurrent();
    update();
}

void ViewportWidget::reloadTerrain() {
    makeCurrent();
    m_terrain.destroy();
    m_basemap.destroy();
    m_heatmap.destroy();
    m_losRay.destroy();
    m_antennaLobe.destroy();
    m_jammerBubble.destroy();
    m_fresnel.destroy();
    const auto& terrain = m_engine->terrain();
    m_terrain.build(terrain);
    m_basemap.build(terrain);
    m_heatmap.build(terrain);
    m_losRay.build();
    m_antennaLobe.build();
    m_jammerBubble.build();
    m_fresnel.build();
    buildMarkers();
    float cx = terrain.worldSize().x * 0.5f;
    float cz = terrain.worldSize().z * 0.5f;
    m_camera.target    = { cx, terrain.heightAtWorld(cx, cz) * kVertScale, cz };
    m_camera.radius    = terrain.worldSize().x * 0.8f;
    m_camera.elevation = 35.0f;
    m_defaultCamera    = m_camera;   // save for R key
    updateMapHud();
    doneCurrent();
    update();
}

void ViewportWidget::mousePressEvent(QMouseEvent* e) {
    m_pressPos  = e->pos();
    m_lastMouse = e->pos();
    m_dragging  = true;
    m_cameraDragging = false;
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragging = false;
        const QPoint delta = e->pos() - m_pressPos;
        if (!m_cameraDragging &&
            delta.manhattanLength() < QApplication::startDragDistance()) {
            if (m_placementActive) {
                float wx = 0.0f, wz = 0.0f;
                if (raycastTerrain(e->pos(), wx, wz))
                    emit terrainClicked(wx, wz);
            } else {
                const int antennaIndex = antennaAtScreenPosition(e->pos());
                if (antennaIndex >= 0) {
                    if (auto* tree = window()->findChild<ScenarioTree*>())
                        tree->selectRow(antennaIndex);
                    emit antennaClicked(antennaIndex);
                }
            }
        }
    } else {
        m_dragging = false;
    }
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
        const glm::vec4 clip = mvp * glm::vec4(antennas[i].position, 1.0f);
        if (clip.w <= 0.0f) continue;

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.05f || ndc.x > 1.05f ||
            ndc.y < -1.05f || ndc.y > 1.05f ||
            ndc.z < -1.0f || ndc.z > 1.0f) {
            continue;
        }

        const float sx = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width());
        const float sy = (0.5f - ndc.y * 0.5f) * static_cast<float>(height());
        const float dx = sx - static_cast<float>(screenPos.x());
        const float dy = sy - static_cast<float>(screenPos.y());
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq <= bestDistanceSq) {
            bestDistanceSq = distanceSq;
            bestIndex = i;
        }
    }
    return bestIndex;
}

void ViewportWidget::setBasemapOpacity(float opacity) {
    m_basemap.setOpacity(opacity);
    update();
}

void ViewportWidget::setPlacementMode(bool active, bool jammer) {
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
        mapText = QString("%1°, %2°\n%3 × %4 km  ·  %5 m terrain grid")
            .arg(center.latDeg, 0, 'f', 5)
            .arg(center.lonDeg, 0, 'f', 5)
            .arg(terrain.worldSize().x / 1000.0f, 0, 'f', 1)
            .arg(terrain.worldSize().z / 1000.0f, 0, 'f', 1)
            .arg(std::max(terrain.metersPerCellX(), terrain.metersPerCellZ()), 0, 'f', 1);
    }
    m_mapInfoLabel->setText(mapText);
    m_mapInfoLabel->adjustSize();

    m_attributionLabel->setText(m_basemap.attribution().isEmpty()
        ? QStringLiteral("Elevation © Mapzen and source contributors")
        : m_basemap.attribution() + "  ·  Elevation © Mapzen and contributors");
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
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragging) {
        if (!m_placementActive) {
            setCursor(antennaAtScreenPosition(e->pos()) >= 0
                ? Qt::PointingHandCursor : Qt::ArrowCursor);
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
    update();
}

void ViewportWidget::wheelEvent(QWheelEvent* e) {
    m_camera.zoom(e->angleDelta().y() / 120.0f);
    update();
}

void ViewportWidget::setOverlayVisible(Overlay overlay, bool visible) {
    switch (overlay) {
        case Basemap: m_basemap.visible      = visible; break;
        case Heatmap: m_heatmap.visible      = visible;
                      m_heatmapLegend->setVisible(visible); break;
        case LOS:     m_losRay.visible       = visible; break;
        case Lobe:    m_antennaLobe.visible  = visible; break;
        case Jammer:  m_jammerBubble.visible = visible; break;
        case Fresnel: m_fresnel.visible      = visible; break;
    }
    update();
}

void ViewportWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Escape:
            if (m_placementActive) {
                setPlacementMode(false);
                emit placementCancelled();
            }
            break;
        case Qt::Key_T:
            m_camera.orbit(0.0f, 89.0f - m_camera.elevation);  // snap to ceiling via orbit
            break;
        case Qt::Key_R:
            if (m_defaultCamera.radius > 0.0f)
                m_camera = m_defaultCamera;    // reset to saved default
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
