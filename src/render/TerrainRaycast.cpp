#include "TerrainRaycast.h"

#include "sim/TerrainModel.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

bool clipAxis(float origin,
              float direction,
              float minimum,
              float maximum,
              float& tEnter,
              float& tExit) {
    constexpr float epsilon = 1.0e-7f;
    if (std::abs(direction) < epsilon)
        return origin >= minimum && origin <= maximum;

    float t0 = (minimum - origin) / direction;
    float t1 = (maximum - origin) / direction;
    if (t0 > t1) std::swap(t0, t1);
    tEnter = std::max(tEnter, t0);
    tExit = std::min(tExit, t1);
    return tEnter <= tExit;
}

// The ray through a pixel's centre, in rendered world coordinates, shared by
// the terrain hit and the plane fallback so the two can never disagree about
// which ray a pixel means.
bool screenRay(const glm::mat4& viewProjection,
               int viewportWidth,
               int viewportHeight,
               float screenX,
               float screenY,
               glm::vec3& origin,
               glm::vec3& direction,
               float& length) {
    // QPoint identifies a logical pixel; use its centre for stable mapping on
    // both standard- and high-DPI framebuffers.
    const float ndcX = 2.0f * (screenX + 0.5f) /
                       static_cast<float>(viewportWidth) - 1.0f;
    const float ndcY = 1.0f - 2.0f * (screenY + 0.5f) /
                       static_cast<float>(viewportHeight);

    const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
    glm::vec4 nearH = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farH = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(nearH.w) < 1.0e-8f || std::abs(farH.w) < 1.0e-8f)
        return false;

    origin = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPoint = glm::vec3(farH) / farH.w;
    const glm::vec3 delta = farPoint - origin;
    length = glm::length(delta);
    if (!std::isfinite(length) || length < 1.0e-5f) return false;
    direction = delta / length;
    return true;
}

} // namespace

bool TerrainRaycast::fromScreen(const TerrainModel& terrain,
                                const glm::mat4& viewProjection,
                                int viewportWidth,
                                int viewportHeight,
                                float screenX,
                                float screenY,
                                float verticalScale,
                                glm::vec3& outTerrainPoint) {
    if (terrain.width < 2 || terrain.height < 2 || terrain.heights.empty() ||
        viewportWidth <= 0 || viewportHeight <= 0 || verticalScale <= 0.0f) {
        return false;
    }

    glm::vec3 rayOrigin{0.0f};
    glm::vec3 rayDirection{0.0f};
    float rayLength = 0.0f;
    if (!screenRay(viewProjection, viewportWidth, viewportHeight, screenX, screenY,
                   rayOrigin, rayDirection, rayLength))
        return false;

    const glm::vec3 worldSize = terrain.worldSize();
    float tEnter = 0.0f;
    float tExit = rayLength;
    if (!clipAxis(rayOrigin.x, rayDirection.x, 0.0f, worldSize.x, tEnter, tExit) ||
        !clipAxis(rayOrigin.z, rayDirection.z, 0.0f, worldSize.z, tEnter, tExit) ||
        tExit < 0.0f) {
        return false;
    }
    tEnter = std::max(0.0f, tEnter);

    const float horizontalSpeed = glm::length(glm::vec2(rayDirection.x, rayDirection.z));
    const float horizontalDistance = (tExit - tEnter) * horizontalSpeed;
    const float terrainSampleM = std::max(
        0.1f, std::min(terrain.metersPerCellX(), terrain.metersPerCellZ()));
    const int steps = std::clamp(
        static_cast<int>(std::ceil(horizontalDistance / (terrainSampleM * 0.5f))),
        64, 8192);

    auto surfaceDifference = [&](float t) {
        const glm::vec3 point = rayOrigin + rayDirection * t;
        return point.y - terrain.heightAtWorld(point.x, point.z) * verticalScale;
    };

    float previousT = tEnter;
    float previousDifference = surfaceDifference(previousT);
    for (int i = 1; i <= steps; ++i) {
        const float fraction = static_cast<float>(i) / static_cast<float>(steps);
        const float currentT = tEnter + (tExit - tEnter) * fraction;
        const float currentDifference = surfaceDifference(currentT);

        if ((previousDifference >= 0.0f && currentDifference <= 0.0f) ||
            (previousDifference <= 0.0f && currentDifference >= 0.0f)) {
            float lowerT = previousT;
            float upperT = currentT;
            float lowerDifference = previousDifference;
            for (int iteration = 0; iteration < 24; ++iteration) {
                const float middleT = (lowerT + upperT) * 0.5f;
                const float middleDifference = surfaceDifference(middleT);
                if ((lowerDifference >= 0.0f && middleDifference >= 0.0f) ||
                    (lowerDifference <= 0.0f && middleDifference <= 0.0f)) {
                    lowerT = middleT;
                    lowerDifference = middleDifference;
                } else {
                    upperT = middleT;
                }
            }

            const glm::vec3 renderedHit =
                rayOrigin + rayDirection * ((lowerT + upperT) * 0.5f);
            outTerrainPoint = {
                std::clamp(renderedHit.x, 0.0f, worldSize.x),
                terrain.heightAtWorld(renderedHit.x, renderedHit.z),
                std::clamp(renderedHit.z, 0.0f, worldSize.z)
            };
            return true;
        }

        previousT = currentT;
        previousDifference = currentDifference;
    }
    return false;
}

bool TerrainRaycast::onHorizontalPlane(const glm::mat4& viewProjection,
                                       int viewportWidth,
                                       int viewportHeight,
                                       float screenX,
                                       float screenY,
                                       float verticalScale,
                                       float planeYUnscaled,
                                       glm::vec2& outXZ) {
    if (viewportWidth <= 0 || viewportHeight <= 0 || !(verticalScale > 0.0f) ||
        !std::isfinite(planeYUnscaled)) {
        return false;
    }
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f};
    float length = 0.0f;
    if (!screenRay(viewProjection, viewportWidth, viewportHeight, screenX, screenY,
                   origin, direction, length))
        return false;
    if (std::abs(direction.y) < 1.0e-6f) return false;

    // The plane is compared in rendered space, where heights are exaggerated,
    // and the returned position is horizontal and therefore unaffected by it.
    const float t = (planeYUnscaled * verticalScale - origin.y) / direction.y;
    if (!(t > 0.0f) || t > length) return false;
    outXZ = {origin.x + direction.x * t, origin.z + direction.z * t};
    return true;
}
