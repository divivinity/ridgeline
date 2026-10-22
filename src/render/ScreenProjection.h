#pragma once
#include <glm/glm.hpp>

// World-to-screen projection shared by everything picked in screen pixels:
// antenna markers and the corner grips of a selected vegetation area. One
// definition, so the two can never disagree about where a point is drawn.
namespace ScreenProjection {

// `renderMvp` is the matrix the scene is drawn with, vertical exaggeration
// included; `world` is an unscaled terrain point. Returns false for a point
// behind the camera, beyond the near or far plane, or more than `margin`
// outside the viewport in normalised device coordinates. Pixels run from the
// top-left, matching widget coordinates.
bool toScreen(const glm::mat4& renderMvp, int width, int height,
              const glm::vec3& world, glm::vec2& outPx, float margin = 1.05f);

} // namespace ScreenProjection
