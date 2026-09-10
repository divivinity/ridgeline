#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <vector>
#include <glm/glm.hpp>
#include "render/ShaderProgram.h"
#include "sim/TerrainModel.h"
#include "sim/Types.h"

class LOSRayOverlay : protected QOpenGLFunctions_3_3_Core {
public:
    bool visible = true;

    void build();
    void update(const std::vector<Antenna>& antennas, const TerrainModel& terrain);
    void updateProbe(const std::vector<Antenna>& antennas,
                     const TerrainModel& terrain,
                     int probeSourceIndex,
                     const glm::vec3* probeReceiver);
    // Draws the line-of-sight rays. Honours `visible` and nothing else.
    void draw(const glm::mat4& mvp);
    // Draws the inspected-point path. This belongs to the result inspector, not
    // to the LOS layer, so it stays on when that layer is hidden. Keeping it
    // inside draw() previously meant one overlay served two concerns and the
    // caller had to draw unconditionally to keep the probe alive.
    void drawProbe(const glm::mat4& mvp);
    void destroy();

private:
    ShaderProgram m_shader;
    GLuint m_vao = 0, m_vbo = 0;
    GLuint m_probeVao = 0, m_probeVbo = 0;
    int    m_vertexCount = 0;
    bool   m_hasProbe = false;
};
