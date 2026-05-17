#include "HeatmapOverlay.h"
#include <vector>
#include <cmath>

struct HVert { float x, y, z, u, v; };

void HeatmapOverlay::build(const TerrainModel& terrain) {
    initializeOpenGLFunctions();
    m_shader.load("assets/shaders/heatmap.vert", "assets/shaders/heatmap.frag");

    // Same mesh as terrain -- reuse vertex positions + UV
    std::vector<HVert> verts;
    verts.reserve(terrain.width * terrain.height);
    for (int z = 0; z < terrain.height; ++z) {
        for (int x = 0; x < terrain.width; ++x) {
            float wx = x * terrain.cellSize;
            float wz = z * terrain.cellSize;
            float wy = terrain.heightAt(x, z);
            float u  = static_cast<float>(x) / (terrain.width  - 1);
            float v  = static_cast<float>(z) / (terrain.height - 1);
            verts.push_back({wx, wy, wz, u, v});
        }
    }
    std::vector<unsigned int> idx;
    idx.reserve((terrain.width - 1) * (terrain.height - 1) * 6);
    for (int z = 0; z < terrain.height - 1; ++z) {
        for (int x = 0; x < terrain.width - 1; ++x) {
            unsigned int tl = z * terrain.width + x;
            unsigned int tr = tl + 1;
            unsigned int bl = tl + terrain.width;
            unsigned int br = bl + 1;
            idx.insert(idx.end(), {tl, bl, tr, tr, bl, br});
        }
    }
    m_indexCount = static_cast<int>(idx.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(HVert), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HVert), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HVert), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Create texture (R32F -- one float per cell)
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void HeatmapOverlay::update(const FieldGrid& grid) {
    if (!grid.valid()) return;
    m_texW = grid.width;
    m_texH = grid.height;

    // Clamp non-finite sentinels so GL_LINEAR filtering can't produce NaN
    // between valid cells and dead-zone cells.
    constexpr float kNoSignal = -121.0f;  // just below uMinDbm (-120)
    std::vector<float> clamped(grid.dbm.size());
    for (size_t i = 0; i < grid.dbm.size(); ++i)
        clamped[i] = std::isfinite(grid.dbm[i]) ? grid.dbm[i] : kNoSignal;

    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, grid.width, grid.height,
                 0, GL_RED, GL_FLOAT, clamped.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void HeatmapOverlay::draw(const glm::mat4& mvp) {
    if (!visible || m_texW == 0) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_shader.bind();
    m_shader.setMat4("uMVP",   mvp);
    m_shader.setFloat("uMinDbm", -120.0f);
    m_shader.setFloat("uMaxDbm",  -40.0f);
    m_shader.setInt("uHeatmap", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    m_shader.release();
    glDisable(GL_BLEND);
}

void HeatmapOverlay::destroy() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
    glDeleteTextures(1, &m_tex);
}
