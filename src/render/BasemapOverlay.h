#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QString>
#include "ShaderProgram.h"
#include "sim/TerrainModel.h"
#include <glm/glm.hpp>
#include <functional>

class BasemapOverlay : protected QOpenGLFunctions_3_3_Core {
public:
    using ProgressCallback = std::function<void(int completed, int total)>;
    using CancelCallback = std::function<bool()>;

    bool visible = true;

    void build(const TerrainModel& terrain);
    void draw(const glm::mat4& mvp);
    void destroy();
    void setOpacity(float opacity);
    void setLoadCallbacks(ProgressCallback progress, CancelCallback cancel) {
        m_loadProgress = std::move(progress);
        m_loadCancel = std::move(cancel);
    }

    bool hasTexture() const { return m_hasTexture; }
    QString attribution() const { return m_attribution; }
    QString providerName() const { return m_providerName; }
    QString lastError() const { return m_lastError; }

private:
    struct MercatorPixel { double x = 0.0; double y = 0.0; };

    static MercatorPixel mercatorPixel(double latDeg, double lonDeg, int zoom);
    static int chooseZoom(const TerrainModel& terrain);

    bool loadTextureForTerrain(const TerrainModel& terrain);

    ShaderProgram m_shader;
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLuint m_tex = 0;
    int    m_indexCount = 0;
    bool   m_hasTexture = false;
    float  m_opacity = 0.82f;
    QString m_attribution;
    QString m_providerName;
    QString m_lastError;
    ProgressCallback m_loadProgress;
    CancelCallback m_loadCancel;
};
