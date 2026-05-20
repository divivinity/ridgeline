#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QString>
#include "ShaderProgram.h"
#include "sim/TerrainModel.h"
#include <glm/glm.hpp>

class BasemapOverlay : protected QOpenGLFunctions_3_3_Core {
public:
    bool visible = true;

    void build(const TerrainModel& terrain);
    void draw(const glm::mat4& mvp);
    void destroy();

private:
    struct MercatorPixel { double x = 0.0; double y = 0.0; };

    static MercatorPixel mercatorPixel(double latDeg, double lonDeg, int zoom);
    static QString tileUrlTemplate();
    static int chooseZoom(const TerrainModel& terrain);

    bool loadTextureForTerrain(const TerrainModel& terrain);

    ShaderProgram m_shader;
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLuint m_tex = 0;
    int    m_indexCount = 0;
    bool   m_hasTexture = false;
    float  m_opacity = 0.82f;
};
