#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QString>
#include <QImage>
#include <QStringList>
#include <vector>
#include "ShaderProgram.h"
#include "sim/TerrainModel.h"
#include <glm/glm.hpp>
#include <functional>

class BasemapOverlay : protected QOpenGLFunctions_3_3_Core {
public:
    using ProgressCallback = std::function<void(int completed, int total)>;
    using CancelCallback = std::function<bool()>;
    using PrepareOpenGLCallback = std::function<void()>;

    bool visible = true;

    void build(const TerrainModel& terrain);
    void draw(const glm::mat4& mvp);
    void destroy();
    void setOpacity(float opacity);
    void setLoadCallbacks(ProgressCallback progress, CancelCallback cancel) {
        m_loadProgress = std::move(progress);
        m_loadCancel = std::move(cancel);
    }
    void setPrepareOpenGLCallback(PrepareOpenGLCallback prepare) {
        m_prepareOpenGL = std::move(prepare);
    }

    // Base rasters to composite, bottom first, each modulated by its opacity.
    // Replaces the single implicit provider: two rasters textured onto the same
    // terrain surface as separate coplanar passes would z-fight, so they are
    // blended into one image before upload.
    struct RasterRequest {
        QString layerId;      // matches LayerStack / TileProviders ids
        float opacity = 1.0f;
    };
    void setRasterLayers(std::vector<RasterRequest> layers) {
        m_rasterLayers = std::move(layers);
    }
    // Multiplied over the composited rasters. A null image disables relief.
    void setHillshade(QImage shade, float opacity) {
        m_hillshade = std::move(shade);
        m_hillshadeOpacity = opacity;
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
    // Per-layer opacity is applied when compositing the raster layers into
    // the texture, so the shader must not dim it a second time.
    float  m_opacity = 1.0f;
    QString m_attribution;
    QStringList m_attributions;
    std::vector<RasterRequest> m_rasterLayers;
    QImage m_hillshade;
    float  m_hillshadeOpacity = 0.0f;
    QString m_providerName;
    QString m_lastError;
    ProgressCallback m_loadProgress;
    CancelCallback m_loadCancel;
    PrepareOpenGLCallback m_prepareOpenGL;
};
