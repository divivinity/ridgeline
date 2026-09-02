#include "HeatmapOverlay.h"
#include <vector>
#include <cmath>
#include <limits>

struct HVert { float x, y, z, u, v; };

void HeatmapOverlay::updateGridMapping(const FieldGrid& grid) {
    const float spanX = static_cast<float>(grid.width) * grid.cellSize;
    const float spanZ = static_cast<float>(grid.height) * grid.cellSize;
    if (!(spanX > 0.0f) || !(spanZ > 0.0f)) {
        m_gridUvScale = {1.0f, 1.0f};
        m_gridUvOffset = {0.0f, 0.0f};
        return;
    }
    m_gridUvScale = {m_terrainWorldXZ.x / spanX,
                     m_terrainWorldXZ.y / spanZ};
    m_gridUvOffset = {
        -(grid.originXZ.x - grid.cellSize * 0.5f) / spanX,
        -(grid.originXZ.y - grid.cellSize * 0.5f) / spanZ};
}

void HeatmapOverlay::build(const TerrainModel& terrain) {
    initializeOpenGLFunctions();
    m_shader.load("assets/shaders/heatmap.vert", "assets/shaders/heatmap.frag");
    const glm::vec3 worldSize = terrain.worldSize();
    m_terrainWorldXZ = {worldSize.x, worldSize.z};

    std::vector<HVert> verts;
    verts.reserve(terrain.width * terrain.height);
    for (int z = 0; z < terrain.height; ++z) {
        for (int x = 0; x < terrain.width; ++x) {
            float wx = x * terrain.metersPerCellX();
            float wz = z * terrain.metersPerCellZ();
            float wy = terrain.heightAt(x, z);
            float u  = static_cast<float>(x) / std::max(1, terrain.width  - 1);
            float v  = static_cast<float>(z) / std::max(1, terrain.height - 1);
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

    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void HeatmapOverlay::update(const FieldGrid& grid, Metric metric) {
    m_metric = metric;
    m_comparisonMode = false;
    if (!grid.valid()) {
        m_texW = 0;
        m_texH = 0;
        return;
    }
    m_texW = grid.width;
    m_texH = grid.height;
    updateGridMapping(grid);

    const std::vector<float>* values = &grid.dbm;
    m_minValue = -120.0f;
    m_maxValue = -40.0f;
    switch (metric) {
        case Metric::DesiredPower:
            values = &grid.dbm; m_minValue = -120.0f; m_maxValue = -40.0f; break;
        case Metric::JammerPower:
            values = &grid.jammerDbm; m_minValue = -120.0f; m_maxValue = -40.0f; break;
        case Metric::Snr:
            values = &grid.snrDb; m_minValue = -20.0f; m_maxValue = 40.0f; break;
        case Metric::Sinr:
            values = &grid.sinrDb; m_minValue = -20.0f; m_maxValue = 40.0f; break;
        case Metric::JOverS:
            values = &grid.jOverSDb; m_minValue = -40.0f; m_maxValue = 40.0f; break;
        case Metric::FadeMargin:
            values = &grid.fadeMarginDb; m_minValue = -20.0f; m_maxValue = 40.0f; break;
        case Metric::Service:
            break;
    }

    std::vector<float> texture(grid.cellCount(), m_minValue - 2.0f);
    if (metric == Metric::Service) {
        // The fragment shader hides values below min+1. With -0.01/1.0 bounds,
        // out-of-service cells are transparent and in-service cells form a
        // crisp service contour over the terrain/basemap.
        m_minValue = -0.01f;
        m_maxValue = 1.0f;
        for (std::size_t i = 0; i < texture.size(); ++i)
            texture[i] = i < grid.service.size() && grid.service[i] ? 1.0f : -1.01f;
    } else if (values && values->size() == texture.size()) {
        const float sentinel = m_minValue - 2.0f;
        for (std::size_t i = 0; i < texture.size(); ++i)
            texture[i] = std::isfinite((*values)[i]) ? (*values)[i] : sentinel;
    }

    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, grid.width, grid.height,
                 0, GL_RED, GL_FLOAT, texture.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void HeatmapOverlay::updateComparison(
    const FieldGrid& grid,
    const std::vector<std::int8_t>& serviceTransition) {
    m_comparisonMode = true;
    if (!grid.valid() || serviceTransition.size() != grid.cellCount()) {
        m_texW = 0;
        m_texH = 0;
        return;
    }

    m_texW = grid.width;
    m_texH = grid.height;
    updateGridMapping(grid);
    std::vector<float> texture(serviceTransition.size(), 0.0f);
    for (std::size_t i = 0; i < serviceTransition.size(); ++i)
        texture[i] = static_cast<float>(serviceTransition[i]);

    glBindTexture(GL_TEXTURE_2D, m_tex);
    // Service transitions are categorical. Linear filtering blends gained
    // and lost cells through a false unchanged band at their boundary.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, grid.width, grid.height,
                 0, GL_RED, GL_FLOAT, texture.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void HeatmapOverlay::draw(const glm::mat4& mvp) {
    if (!visible || m_texW == 0) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    m_shader.bind();
    m_shader.setMat4("uMVP", mvp);
    m_shader.setFloat("uMinDbm", m_minValue);
    m_shader.setFloat("uMaxDbm", m_maxValue);
    m_shader.setInt("uHeatmap", 0);
    m_shader.setInt("uComparisonMode", m_comparisonMode ? 1 : 0);
    m_shader.setVec2("uGridUvScale", m_gridUvScale);
    m_shader.setVec2("uGridUvOffset", m_gridUvOffset);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    m_shader.release();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void HeatmapOverlay::destroy() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
    glDeleteTextures(1, &m_tex);
    m_vao = m_vbo = m_ebo = m_tex = 0;
    m_texW = m_texH = 0;
    m_shader.destroy();
}
