#include "FresnelOverlay.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

static const float PI = 3.14159265f;

static void buildUnitSphere(int stacks, int slices,
                             std::vector<float>& verts,
                             std::vector<unsigned>& idx) {
    for (int i = 0; i <= stacks; ++i) {
        float phi = PI * i / stacks;
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * PI * j / slices;
            verts.push_back(std::sin(phi) * std::cos(theta));
            verts.push_back(std::cos(phi));
            verts.push_back(std::sin(phi) * std::sin(theta));
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            unsigned a = i * (slices + 1) + j;
            unsigned b = a + slices + 1;
            idx.insert(idx.end(), {a, b, a+1u, b, b+1u, a+1u});
        }
    }
}

// Build a rotation matrix that maps local Z to the given direction vector.
static glm::mat4 rotationToDir(glm::vec3 dir) {
    glm::vec3 ref = (std::abs(dir.y) < 0.9f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    glm::vec3 right = glm::normalize(glm::cross(ref, dir));
    glm::vec3 up    = glm::cross(dir, right);
    glm::mat4 r(1.0f);
    r[0] = glm::vec4(right, 0);
    r[1] = glm::vec4(up,    0);
    r[2] = glm::vec4(dir,   0);
    return r;
}

void FresnelOverlay::build() {
    initializeOpenGLFunctions();
    m_shader.load("assets/shaders/sphere.vert", "assets/shaders/sphere.frag");

    std::vector<float>    verts;
    std::vector<unsigned> idx;
    buildUnitSphere(16, 24, verts, idx);
    m_indexCount = static_cast<int>(idx.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void FresnelOverlay::update(const std::vector<Antenna>& antennas,
                             const TerrainModel& terrain) {
    m_ellipsoids.clear();

    // Build one ellipsoid for each non-jammer antenna pair.
    for (size_t i = 0; i < antennas.size(); ++i) {
        if (antennas[i].isJammer) continue;
        for (size_t j = i + 1; j < antennas.size(); ++j) {
            if (antennas[j].isJammer) continue;

            const glm::vec3& A = antennas[i].position;
            const glm::vec3& B = antennas[j].position;
            float d = glm::distance(A, B);
            if (d < 1.0f) continue;

            // First Fresnel zone: λ = c/f, r1_midpoint = sqrt(λd/4)
            float freqHz = (antennas[i].frequencyMHz + antennas[j].frequencyMHz)
                           * 0.5f * 1.0e6f;
            float lambda = 3.0e8f / freqHz;
            float r1     = std::sqrt(lambda * d / 4.0f);
            r1 = std::max(r1, 1.0f);  // at least 1m so it's always visible

            glm::vec3 mid = (A + B) * 0.5f;
            glm::vec3 los = glm::normalize(B - A);

            // Ellipsoid: semi-axis a = d/2 (along LOS), b = r1 (transverse)
            glm::mat4 model =
                glm::translate(glm::mat4(1.0f), mid) *
                rotationToDir(los) *
                glm::scale(glm::mat4(1.0f), glm::vec3(r1, r1, d * 0.5f));

            // Color by LOS status
            auto status = terrain.lineOfSightStatus(A, B);
            glm::vec4 color =
                (status == TerrainModel::LOSStatus::Clear)      ? glm::vec4(0.2f, 1.0f, 0.4f, 0.10f)
              : (status == TerrainModel::LOSStatus::Diffracted)  ? glm::vec4(1.0f, 0.8f, 0.1f, 0.10f)
              :                                                     glm::vec4(1.0f, 0.2f, 0.2f, 0.10f);

            m_ellipsoids.push_back({model, color});
        }
    }
}

void FresnelOverlay::draw(const glm::mat4& vp) {
    if (!visible || m_ellipsoids.empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_shader.bind();
    glBindVertexArray(m_vao);
    for (const auto& e : m_ellipsoids) {
        m_shader.setMat4("uMVP", vp * e.model);
        m_shader.setVec4("uColor", e.color);
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    m_shader.release();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void FresnelOverlay::destroy() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
}
