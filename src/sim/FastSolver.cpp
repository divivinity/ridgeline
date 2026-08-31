#include "FastSolver.h"
#include "RFModels.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <glm/glm.hpp>
#include <algorithm>

static constexpr float NEG_INF = -std::numeric_limits<float>::infinity();

FieldGrid FastSolver::solve(const TerrainModel& terrain,
                             const std::vector<Antenna>& antennas,
                             const MaterialLibrary& materials,
                             const std::function<void(int, int)>& progress,
                             const std::function<bool()>& cancelled) const {
    FieldGrid grid;
    grid.cellSize = gridResolution;
    const glm::vec3 world = terrain.worldSize();
    grid.width    = std::max(1, static_cast<int>(std::ceil(world.x / gridResolution)));
    grid.height   = std::max(1, static_cast<int>(std::ceil(world.z / gridResolution)));
    grid.originXZ = {gridResolution * 0.5f, gridResolution * 0.5f};
    grid.dbm.assign(grid.width * grid.height, NEG_INF);
    RFModels::initializeMetrics(grid);
    if (progress) progress(0, grid.height);

    const Material& mat = materials.defaultMaterial();

    for (int gz = 0; gz < grid.height; ++gz) {
        if (cancelled && cancelled())
            throw std::runtime_error("Coverage solve cancelled");
        for (int gx = 0; gx < grid.width; ++gx) {
            if ((gx & 15) == 0 && cancelled && cancelled())
                throw std::runtime_error("Coverage solve cancelled");

            const float wx = gx * gridResolution + gridResolution * 0.5f;
            const float wz = gz * gridResolution + gridResolution * 0.5f;
            const float wy = terrain.heightAtWorld(wx, wz) + 1.5f;
            const glm::vec3 rxPos{wx, wy, wz};

            float bestDesiredDbm = NEG_INF;
            float jammerMw = 0.0f;
            for (const auto& ant : antennas) {
                float distM = glm::distance(ant.position, rxPos);
                if (distM < 1.0f) distM = 1.0f;

                const float txPowerDbm = 10.0f * std::log10(
                    std::max(ant.powerW, 1.0e-9f) * 1000.0f);
                const float directionalGain = RFModels::effectiveAntennaGainDbi(
                    ant, rxPos, receiver);
                const float matLossDb = mat.lossDbPer100m(ant.frequencyMHz)
                                      * distM / 100.0f;
                float loss = fsplDb(distM, ant.frequencyMHz)
                           - directionalGain
                           + matLossDb;

                if (!terrain.lineOfSight(ant.position, rxPos)) loss += 20.0f;
                const float rxDbm = txPowerDbm - loss;
                if (ant.isJammer)
                    jammerMw += RFModels::dbmToMw(rxDbm);
                else if (rxDbm > bestDesiredDbm)
                    bestDesiredDbm = rxDbm;
            }

            const std::size_t index = static_cast<std::size_t>(gz) * grid.width + gx;
            grid.dbm[index] = bestDesiredDbm;
            grid.jammerDbm[index] = RFModels::mwToDbm(jammerMw);
        }
        if (progress) progress(gz + 1, grid.height);
    }
    RFModels::finalizeMetrics(grid, receiver);
    return grid;
}

float FastSolver::fsplDb(float distM, float freqMHz) {
    return 20.0f * std::log10(distM)
         + 20.0f * std::log10(freqMHz * 1e6f)
         - 147.55f;
}
