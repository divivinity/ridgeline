#include "VegetationAreaOverlay.h"

#include <cstddef>

using VegetationGeometry::LineVertex;

void VegetationAreaOverlay::build() {
    initializeOpenGLFunctions();
    m_shader.load("assets/shaders/outline.vert", "assets/shaders/outline.frag");

    const GLsizei stride = static_cast<GLsizei>(sizeof(LineVertex));
    for (Buffer& buffer : m_buffers) {
        glGenVertexArrays(1, &buffer.vao);
        glGenBuffers(1, &buffer.vbo);
        glBindVertexArray(buffer.vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer.vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(LineVertex, x)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(LineVertex, r)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(LineVertex, along)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(LineVertex, dashed)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(LineVertex, slopeX)));
        glEnableVertexAttribArray(4);
        buffer.count = 0;
    }
    glBindVertexArray(0);
    m_built = true;
}

void VegetationAreaOverlay::upload(Slot slot, const std::vector<LineVertex>& vertices) {
    if (!m_built) return;
    Buffer& buffer = m_buffers[static_cast<int>(slot)];
    glBindBuffer(GL_ARRAY_BUFFER, buffer.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(LineVertex)),
                 vertices.empty() ? nullptr : vertices.data(), GL_DYNAMIC_DRAW);
    buffer.count = static_cast<int>(vertices.size());
}

void VegetationAreaOverlay::drawSlot(const Buffer& buffer) {
    if (buffer.count == 0) return;
    glBindVertexArray(buffer.vao);
    glDrawArrays(GL_LINES, 0, buffer.count);
}

void VegetationAreaOverlay::draw(const DrawParams& params) {
    if (!m_built) return;
    const bool show[3] = {params.committed, params.selection, params.preview};
    bool anything = false;
    for (int i = 0; i < 3; ++i)
        anything = anything || (show[i] && m_buffers[i].count > 0);
    if (!anything) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Lines test against the terrain but never write depth, so the coverage
    // drape and markers drawn afterwards are unaffected.
    glDepthMask(GL_FALSE);
    m_shader.bind();
    m_shader.setMat4("uMVP", params.mvp);
    m_shader.setVec3("uEye", params.eyeUnscaled);
    m_shader.setFloat("uLiftM", kLiftM);
    m_shader.setFloat("uPull", kPull);
    m_shader.setVec2("uViewportPx", params.viewportPx);
    m_shader.setFloat("uVertScale", params.vertScale);
    m_shader.setFloat("uPixelAngle", params.pixelAngleRad);
    m_shader.setFloat("uDashPeriodM", params.dashPeriodM);

    // Every halo first, then every core, so one slot's dark halo never lands on
    // another slot's light line.
    m_shader.setInt("uHalo", 1);
    const glm::vec2 offsets[4] = {{1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f}};
    for (const glm::vec2& offset : offsets) {
        m_shader.setVec2("uPixelOffset", offset);
        for (int i = 0; i < 3; ++i)
            if (show[i]) drawSlot(m_buffers[i]);
    }
    m_shader.setInt("uHalo", 0);
    m_shader.setVec2("uPixelOffset", glm::vec2(0.0f));
    for (int i = 0; i < 3; ++i)
        if (show[i]) drawSlot(m_buffers[i]);

    glBindVertexArray(0);
    m_shader.release();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void VegetationAreaOverlay::destroy() {
    for (Buffer& buffer : m_buffers) {
        if (buffer.vao) glDeleteVertexArrays(1, &buffer.vao);
        if (buffer.vbo) glDeleteBuffers(1, &buffer.vbo);
        buffer = Buffer{};
    }
    m_shader.destroy();
    m_built = false;
}
