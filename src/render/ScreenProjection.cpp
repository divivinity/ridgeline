#include "render/ScreenProjection.h"

bool ScreenProjection::toScreen(const glm::mat4& renderMvp, int width, int height,
                                const glm::vec3& world, glm::vec2& outPx, float margin) {
    if (width <= 0 || height <= 0) return false;
    const glm::vec4 clip = renderMvp * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f) return false;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.x < -margin || ndc.x > margin ||
        ndc.y < -margin || ndc.y > margin ||
        ndc.z < -1.0f || ndc.z > 1.0f) {
        return false;
    }
    outPx = {(ndc.x * 0.5f + 0.5f) * static_cast<float>(width),
             (0.5f - ndc.y * 0.5f) * static_cast<float>(height)};
    return true;
}
