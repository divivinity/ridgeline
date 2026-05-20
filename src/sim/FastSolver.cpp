#include "FastSolver.h"
#include <cmath>
#include <limits>
#include <glm/glm.hpp>
#include <algorithm>

static constexpr float NEG_INF = -std::numeric_limits<float>::infinity();

FieldGrid FastSolver::solve(const TerrainModel& terrain,
                             const std::vector<Antenna>& antennas,
                             const MaterialLibrary& materials) const {
    FieldGrid grid;
    grid.cellSize = gridResolution;
    const glm::vec3 world = terrain.worldSize();
    grid.width    = std::max(1, static_cast<int>(std::ceil(world.x / gridResolution)));
    grid.height   = std::max(1, static_cast<int>(std::ceil(world.z / gridResolution)));
    grid.originXZ = {0.0f, 0.0f};
    grid.dbm.assign(grid.width * grid.height, NEG_INF);

    const Material& mat = materials.defaultMaterial();

    for (int gz = 0; gz < grid.height; ++gz) {
        for (int gx = 0; gx < grid.width; ++gx) {
            float wx = gx * gridResolution + gridResolution * 0.5f;
            float wz = gz * gridResolution + gridResolution * 0.5f;
            float wy = terrain.heightAtWorld(wx, wz) + 1.5f;

            float bestDbm = NEG_INF;
            for (const auto& ant : antennas) {
                if (ant.isJammer) continue;

                glm::vec3 rxPos{wx, wy, wz};
                float distM = glm::distance(ant.position, rxPos);
                if (distM < 1.0f) distM = 1.0f;

                float txPowerDbm = 10.0f * std::log10(ant.powerW * 1000.0f);
                float matLossDb  = mat.lossDbPer100m(ant.frequencyMHz) * distM / 100.0f;
                float loss = fsplDb(distM, ant.frequencyMHz)
                           - ant.gainDbi
                           + matLossDb;

                bool los = terrain.lineOfSight(ant.position, rxPos);
                if (!los) loss += 20.0f;

                float rxDbm = txPowerDbm - loss;
                if (rxDbm > bestDbm) bestDbm = rxDbm;
            }
            grid.dbm[gz * grid.width + gx] = bestDbm;
        }
    }
    return grid;
}

float FastSolver::fsplDb(float distM, float freqMHz) {
    return 20.0f * std::log10(distM)
         + 20.0f * std::log10(freqMHz * 1e6f)
         - 147.55f;
}
