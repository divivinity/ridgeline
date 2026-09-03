#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include "render/ShaderProgram.h"
#include "sim/Types.h"
#include "sim/TerrainModel.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class HeatmapOverlay : protected QOpenGLFunctions_3_3_Core {
public:
    enum class Metric {
        DesiredPower = 0,
        JammerPower,
        Snr,
        Sinr,
        JOverS,
        FadeMargin,
        Service
    };

    bool visible = true;

    void build(const TerrainModel& terrain);
    void update(const FieldGrid& grid, Metric metric = Metric::DesiredPower);
    void updateComparison(const FieldGrid& grid,
                          const std::vector<std::int8_t>& serviceTransition);
    void draw(const glm::mat4& mvp);
    void destroy();
    Metric metric() const { return m_metric; }

private:
    ShaderProgram m_shader;
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLuint m_tex = 0;
    int    m_indexCount = 0;
    int    m_texW = 0, m_texH = 0;
    float  m_minValue = -120.0f;
    float  m_maxValue = -40.0f;
    Metric m_metric = Metric::DesiredPower;
    bool   m_comparisonMode = false;
    glm::vec2 m_terrainWorldXZ{1.0f, 1.0f};
    glm::vec2 m_gridUvScale{1.0f, 1.0f};
    glm::vec2 m_gridUvOffset{0.0f, 0.0f};

    void updateGridMapping(const FieldGrid& grid);
};
