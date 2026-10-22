#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include "map/VegetationGeometry.h"
#include "render/ShaderProgram.h"

#include <glm/glm.hpp>
#include <vector>

// Draws drawn-vegetation outlines: every committed area, the selection with the
// solver's footprint around it, and the rectangle being dragged.
//
// Holds GL objects only. The geometry comes from VegetationGeometry, built on
// the CPU and handed over through upload(), which the viewport calls from
// paintGL where its context is known to be current - never from a mouse
// handler.
class VegetationAreaOverlay : protected QOpenGLFunctions_3_3_Core {
public:
    enum class Slot { Committed = 0, Selection = 1, Preview = 2 };

    struct DrawParams {
        glm::mat4 mvp{1.0f};          // the scene matrix, vertical exaggeration included
        glm::vec3 eyeUnscaled{0.0f};  // camera position in terrain metres
        glm::vec2 viewportPx{1.0f, 1.0f};
        float vertScale = 1.0f;       // vertical exaggeration inside mvp
        float pixelAngleRad = 0.001f; // vertical field of view / framebuffer height
        float dashPeriodM = 10.0f;
        bool committed = true;
        bool selection = false;
        bool preview = false;
    };

    // True metres above the mesh. The imagery drape sits 0.15 m up and writes
    // depth, so an outline at mesh height would be hidden under it up close.
    static constexpr float kLiftM = 0.5f;
    // A 24-bit depth buffer with a 1 m near plane resolves about d^2 / 2^24 m at
    // distance d; pulling by 0.4% of d stays ahead of that out to about 67 km,
    // beyond the largest terrain Ridgeline loads. The shader adds to this a pull
    // that grows with how obliquely the ground under the line is seen, to stay
    // ahead of the imagery drape's slope-scaled polygon offset.
    static constexpr float kPull = 0.004f;

    void build();
    void upload(Slot slot, const std::vector<VegetationGeometry::LineVertex>& vertices);
    void draw(const DrawParams& params);
    void destroy();
    bool ready() const { return m_built; }

private:
    struct Buffer {
        GLuint vao = 0;
        GLuint vbo = 0;
        int count = 0;
    };

    void drawSlot(const Buffer& buffer);

    ShaderProgram m_shader;
    Buffer m_buffers[3];
    bool m_built = false;
};
