#include "LOSRayOverlay.h"
#include <glm/glm.hpp>

// Vertex: position (xyz) + color (rgb)
struct LOSVert { float x, y, z, r, g, b; };

// Colors: blue = clear, orange = diffracted, red = blocked
static constexpr glm::vec3 kClear      { 0.2f, 0.6f, 1.0f };
static constexpr glm::vec3 kDiffracted { 1.0f, 0.6f, 0.1f };
static constexpr glm::vec3 kBlocked    { 1.0f, 0.2f, 0.2f };

void LOSRayOverlay::build() {
    initializeOpenGLFunctions();
    m_shader.load("assets/shaders/overlay.vert", "assets/shaders/overlay.frag");

    auto buildBuffer = [this](GLuint& vao, GLuint& vbo) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LOSVert), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LOSVert),
                              (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
    };
    buildBuffer(m_vao, m_vbo);
    buildBuffer(m_probeVao, m_probeVbo);
    glBindVertexArray(0);
}

void LOSRayOverlay::update(const std::vector<Antenna>& antennas,
                           const TerrainModel& terrain) {
    std::vector<LOSVert> verts;

    for (size_t i = 0; i < antennas.size(); ++i) {
        for (size_t j = i + 1; j < antennas.size(); ++j) {
            const glm::vec3& a = antennas[i].position;
            const glm::vec3& b = antennas[j].position;

            auto status = terrain.lineOfSightStatus(a, b);
            glm::vec3 col = (status == TerrainModel::LOSStatus::Clear)      ? kClear
                          : (status == TerrainModel::LOSStatus::Diffracted)  ? kDiffracted
                          : kBlocked;

            verts.push_back({a.x, a.y, a.z, col.r, col.g, col.b});
            verts.push_back({b.x, b.y, b.z, col.r, col.g, col.b});
        }
    }

    m_vertexCount = static_cast<int>(verts.size());
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(LOSVert),
                 verts.empty() ? nullptr : verts.data(), GL_DYNAMIC_DRAW);
}

void LOSRayOverlay::updateProbe(const std::vector<Antenna>& antennas,
                                const TerrainModel& terrain,
                                int probeSourceIndex,
                                const glm::vec3* probeReceiver) {
    m_hasProbe = probeReceiver && probeSourceIndex >= 0 &&
                 probeSourceIndex < static_cast<int>(antennas.size());
    glBindBuffer(GL_ARRAY_BUFFER, m_probeVbo);
    if (!m_hasProbe) {
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        return;
    }
    const glm::vec3& source = antennas[probeSourceIndex].position;
    const auto status = terrain.lineOfSightStatus(source, *probeReceiver);
    const glm::vec3 color =
        status == TerrainModel::LOSStatus::Clear ? kClear :
        status == TerrainModel::LOSStatus::Diffracted ? kDiffracted : kBlocked;
    const LOSVert vertices[2]{
        {source.x, source.y, source.z, color.r, color.g, color.b},
        {probeReceiver->x, probeReceiver->y, probeReceiver->z,
         color.r, color.g, color.b}
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
}

void LOSRayOverlay::draw(const glm::mat4& mvp) {
    if ((!visible || m_vertexCount == 0) && !m_hasProbe) return;
    glLineWidth(2.0f);
    m_shader.bind();
    m_shader.setMat4("uMVP", mvp);
    if (visible && m_vertexCount > 0) {
        glBindVertexArray(m_vao);
        glDrawArrays(GL_LINES, 0, m_vertexCount);
    }
    if (m_hasProbe) {
        glBindVertexArray(m_probeVao);
        glDrawArrays(GL_LINES, 0, 2);
    }
    glBindVertexArray(0);
    m_shader.release();
    glLineWidth(1.0f);
}

void LOSRayOverlay::destroy() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_probeVao);
    glDeleteBuffers(1, &m_probeVbo);
    m_vao = m_vbo = m_probeVao = m_probeVbo = 0;
    m_vertexCount = 0;
    m_hasProbe = false;
}
