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

} // namespace TerrainRaycast
