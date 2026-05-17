#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include "ShaderProgram.h"
#include "sim/TerrainModel.h"
#include <glm/glm.hpp>

class TerrainMesh : protected QOpenGLFunctions_3_3_Core {
public:
    void build(const TerrainModel& terrain);
    void draw(const glm::mat4& mvp);
    void destroy();

private:
    ShaderProgram m_shader;
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    int    m_indexCount = 0;
};
