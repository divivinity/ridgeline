#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <vector>
#include <glm/glm.hpp>
#include "render/ShaderProgram.h"
#include "sim/Types.h"
#include "sim/TerrainModel.h"

class FresnelOverlay : protected QOpenGLFunctions_3_3_Core {
public:
    bool visible = true;

    void build();
    void update(const std::vector<Antenna>& antennas, const TerrainModel& terrain);
    void draw(const glm::mat4& vp);
    void destroy();

private:
    struct Ellipsoid { glm::mat4 model; glm::vec4 color; };

    ShaderProgram          m_shader;
    GLuint                 m_vao = 0, m_vbo = 0, m_ebo = 0;
    int                    m_indexCount = 0;
    std::vector<Ellipsoid> m_ellipsoids;
};
