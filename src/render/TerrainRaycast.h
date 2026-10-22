#pragma once

#include <glm/glm.hpp>

class TerrainModel;

namespace TerrainRaycast {

// viewProjection is the camera projection * view matrix in rendered world
// coordinates. Terrain heights are scaled only for intersection; returned
// coordinates remain in the TerrainModel's unscaled world coordinate system.
bool fromScreen(const TerrainModel& terrain,
                const glm::mat4& viewProjection,
                int viewportWidth,
                int viewportHeight,
                float screenX,
                float screenY,
                float verticalScale,
                glm::vec3& outTerrainPoint);

// Where the ray through a pixel meets the horizontal plane at `planeYUnscaled`
// (terrain metres, before vertical exaggeration).
//
// The fallback for a drag that leaves the terrain. Holding the gesture on the
// plane through its anchor keeps a corner under the cursor; the alternative is
// letting it jump to wherever the ray next meets ground, which at grazing
// angles can be kilometres away. False when the ray is parallel to the plane or
// meets it behind the camera or beyond the far plane.
bool onHorizontalPlane(const glm::mat4& viewProjection,
                       int viewportWidth,
                       int viewportHeight,
                       float screenX,
                       float screenY,
                       float verticalScale,
                       float planeYUnscaled,
                       glm::vec2& outXZ);

} // namespace TerrainRaycast
