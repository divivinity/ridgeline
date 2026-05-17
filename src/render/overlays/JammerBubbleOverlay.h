#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <vector>
#include <glm/glm.hpp>
#include "render/ShaderProgram.h"
#include "sim/Types.h"

class JammerBubbleOverlay : protected QOpenGLFunctions_3_3_Core {
public:
    bool visible = true;

    void build();
    void update(const std::vector<Antenna>& antennas);
    void draw(const glm::mat4& vp);
    void destroy();

private:
    ShaderProgram          m_shader;
    GLuint                 m_vao = 0, m_vbo = 0, m_ebo = 0;
    int                    m_indexCount = 0;
    std::vector<glm::mat4> m_models;
};
