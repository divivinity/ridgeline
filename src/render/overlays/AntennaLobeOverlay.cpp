#include "AntennaLobeOverlay.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

static void buildUnitSphere(int stacks, int slices,
                             std::vector<float>& verts,
                             std::vector<unsigned>& idx) {
    const float PI = 3.14159265f;
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

void AntennaLobeOverlay::build() {
    initializeOpenGLFunctions();
    m_shader.load("assets/shaders/sphere.vert", "assets/shaders/sphere.frag");

    std::vector<float> verts;
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

void AntennaLobeOverlay::update(const std::vector<Antenna>& antennas) {
    m_models.clear();
    const float pi = 3.14159265f;
    for (const auto& ant : antennas) {
        if (ant.isJammer) continue;
        const float r = 100.0f * std::pow(10.0f, ant.gainDbi / 20.0f);

        float crossX = r;
        float crossY = r;
        float forward = r;
        if (ant.horizontalBeamwidthDeg < 359.0f) {
            crossX = r * std::max(0.12f, std::sin(
                std::clamp(ant.horizontalBeamwidthDeg, 1.0f, 180.0f) * 0.5f * pi / 180.0f));
            forward = r * 1.35f;
        }
        if (ant.verticalBeamwidthDeg < 179.0f) {
            crossY = r * std::max(0.12f, std::sin(
                std::clamp(ant.verticalBeamwidthDeg, 1.0f, 180.0f) * 0.5f * pi / 180.0f));
        }

        glm::mat4 model = glm::translate(glm::mat4(1.0f), ant.position);
        model = glm::rotate(model, glm::radians(ant.azimuthDeg), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(ant.downtiltDeg), glm::vec3(1, 0, 0));
        model = glm::scale(model, glm::vec3(crossX, crossY, forward));
        m_models.push_back(model);
    }
}

void AntennaLobeOverlay::draw(const glm::mat4& vp) {
    if (!visible || m_models.empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_shader.bind();
    m_shader.setVec4("uColor", {0.28f, 0.78f, 1.0f, 0.13f});
    glBindVertexArray(m_vao);
    for (const auto& model : m_models) {
        m_shader.setMat4("uMVP", vp * model);
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    m_shader.release();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void AntennaLobeOverlay::destroy() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
}
