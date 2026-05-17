#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include "render/ShaderProgram.h"
#include "sim/Types.h"
#include "sim/TerrainModel.h"
#include <glm/glm.hpp>

class HeatmapOverlay : protected QOpenGLFunctions_3_3_Core {
public:
    bool visible = true;

    void build(const TerrainModel& terrain);
    void update(const FieldGrid& grid);
    void draw(const glm::mat4& mvp);
    void destroy();

private:
    ShaderProgram m_shader;
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLuint m_tex = 0;
    int    m_indexCount = 0;
    int    m_texW = 0, m_texH = 0;
};
