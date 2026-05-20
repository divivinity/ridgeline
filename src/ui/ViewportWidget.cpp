#include "ViewportWidget.h"
#include <QMouseEvent>
#include <QWheelEvent>

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
    m_lastMouse = e->pos();
    m_dragging  = true;
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent*) {
    m_dragging = false;
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
