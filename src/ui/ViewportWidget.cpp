#include "ViewportWidget.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>

ViewportWidget::ViewportWidget(SimEngine* engine, QWidget* parent)
    : QOpenGLWidget(parent), m_engine(engine)
{
    setFocusPolicy(Qt::StrongFocus);
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

    // Center camera on terrain
    float cx = terrain.worldSize().x * 0.5f;
    float cz = terrain.worldSize().z * 0.5f;
    m_camera.target  = { cx, terrain.heightAtWorld(cx, cz), cz };
    m_camera.radius  = terrain.worldSize().x * 0.8f;
    m_camera.elevation = 35.0f;
    m_defaultCamera = m_camera;   // save for R key
}

void ViewportWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void ViewportWidget::paintGL() {
    if (height() == 0) return;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    float aspect = static_cast<float>(width()) / height();
    glm::mat4 mvp = m_camera.mvp(aspect);

    m_terrain.draw(mvp);
    if (m_basemap.visible)      m_basemap.draw(mvp);
    if (m_heatmap.visible)      m_heatmap.draw(mvp);
    if (m_losRay.visible)       m_losRay.draw(mvp);
    if (m_antennaLobe.visible)  m_antennaLobe.draw(mvp);
    if (m_jammerBubble.visible) m_jammerBubble.draw(mvp);
    if (m_fresnel.visible)      m_fresnel.draw(mvp);
}

void ViewportWidget::refreshOverlays() {
    makeCurrent();
    m_heatmap.update(m_engine->fieldGrid());
    m_losRay.update(m_engine->antennas(), m_engine->terrain());
    m_antennaLobe.update(m_engine->antennas());
    m_jammerBubble.update(m_engine->antennas());
    m_fresnel.update(m_engine->antennas(), m_engine->terrain());
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
    float cx = terrain.worldSize().x * 0.5f;
    float cz = terrain.worldSize().z * 0.5f;
    m_camera.target    = { cx, terrain.heightAtWorld(cx, cz), cz };
    m_camera.radius    = terrain.worldSize().x * 0.8f;
    m_camera.elevation = 35.0f;
    m_defaultCamera    = m_camera;   // save for R key
    doneCurrent();
    update();
}

void ViewportWidget::mousePressEvent(QMouseEvent* e) {
    m_pressPos  = e->pos();
    m_lastMouse = e->pos();
    m_dragging  = true;
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragging = false;
        // Short click (not a drag) → place antenna
        QPoint delta = e->pos() - m_pressPos;
        if (delta.manhattanLength() < 6) {
            float wx = 0.0f, wz = 0.0f;
            if (raycastTerrain(e->pos(), wx, wz))
                emit terrainClicked(wx, wz);
        }
    } else {
        m_dragging = false;
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragging) return;
    QPoint delta = e->pos() - m_lastMouse;
    m_lastMouse  = e->pos();
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
        case Heatmap: m_heatmap.visible      = visible; break;
        case LOS:     m_losRay.visible       = visible; break;
        case Lobe:    m_antennaLobe.visible  = visible; break;
        case Jammer:  m_jammerBubble.visible = visible; break;
        case Fresnel: m_fresnel.visible      = visible; break;
    }
    update();
}

void ViewportWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
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
    const TerrainModel& terrain = m_engine->terrain();
    if (terrain.width < 2 || terrain.height < 2) return false;

    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    const glm::mat4 invMVP = glm::inverse(m_camera.mvp(aspect));

    // Convert screen pixel to NDC
    const float ndcX =  2.0f * screenPos.x() / static_cast<float>(width())  - 1.0f;
    const float ndcY = -2.0f * screenPos.y() / static_cast<float>(height()) + 1.0f;

    // Unproject near and far points
    const glm::vec4 nearH = invMVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    const glm::vec4 farH  = invMVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    const glm::vec3 nearW = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farW  = glm::vec3(farH)  / farH.w;

    const glm::vec3 rayDir = glm::normalize(farW - nearW);
    if (std::abs(rayDir.y) < 1e-6f) return false;

    // Ray-march against terrain (200 steps over the terrain diagonal)
    const float terrainW = terrain.width  * terrain.metersPerCellX();
    const float terrainH = terrain.height * terrain.metersPerCellZ();
    const float maxDist  = 2.0f * std::sqrt(terrainW * terrainW + terrainH * terrainH
                                            + 8000.0f * 8000.0f);
    const int   steps    = 200;
    const float stepSize = maxDist / steps;

    float prevAbove = 0.0f;
    glm::vec3 prevP = nearW;
    for (int i = 1; i <= steps; ++i) {
        const float t = i * stepSize;
        const glm::vec3 p = nearW + rayDir * t;

        // Skip if outside terrain XZ bounds
        if (p.x < 0.0f || p.x > terrainW || p.z < 0.0f || p.z > terrainH)
            continue;

        const int gx = std::clamp(static_cast<int>(p.x / terrain.metersPerCellX()),
                                  0, terrain.width  - 1);
        const int gz = std::clamp(static_cast<int>(p.z / terrain.metersPerCellZ()),
                                  0, terrain.height - 1);
        const float groundY = terrain.heightAt(gx, gz);
        const float above   = p.y - groundY;

        if (i > 1 && above < 0.0f) {
            // Crossed the surface — interpolate for better accuracy
            const float frac = prevAbove / (prevAbove - above);
            const glm::vec3 hit = prevP + (p - prevP) * frac;
            outWorldX = hit.x;
            outWorldZ = hit.z;
            return true;
        }
        prevAbove = above;
        prevP     = p;
    }
    return false;
}
