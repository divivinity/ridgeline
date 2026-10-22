#include "TerrainMesh.h"
#include "TerrainTriangulation.h"
#include <vector>

struct Vertex { float x, y, z, u, v; };

void TerrainMesh::build(const TerrainModel& terrain) {
    initializeOpenGLFunctions();
    m_shader.load("assets/shaders/terrain.vert", "assets/shaders/terrain.frag");

    std::vector<Vertex> verts;
    verts.reserve(terrain.width * terrain.height);
    for (int z = 0; z < terrain.height; ++z) {
        for (int x = 0; x < terrain.width; ++x) {
            float wx = x * terrain.metersPerCellX();
            float wz = z * terrain.metersPerCellZ();
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
            TerrainTriangulation::appendCellIndices(idx, tl, tr, bl, br);
        }
    }
    m_indexCount = static_cast<int>(idx.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int),
                 idx.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void TerrainMesh::draw(const glm::mat4& mvp) {
    m_shader.bind();
    m_shader.setMat4("uMVP", mvp);
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    m_shader.release();
}

void TerrainMesh::destroy() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
}
