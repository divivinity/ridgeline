#include "FastSolver.h"
#include "RFModels.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <algorithm>

static constexpr float NEG_INF = -std::numeric_limits<float>::infinity();

namespace {
struct Contribution {
    float frequencyMHz = 0.0f;
    float powerMw = 0.0f;
    int sourceIndex = -1;
};
}

FieldGrid FastSolver::solve(const TerrainModel& terrain,
                             const std::vector<Antenna>& antennas,
                             const MaterialLibrary& materials,
                             const std::function<void(int, int)>& progress,
                             const std::function<bool()>& cancelled) const {
    FieldGrid grid;
    grid.cellSize = gridResolution;
    const glm::vec3 world = terrain.worldSize();
    grid.width = std::max(1, static_cast<int>(std::ceil(world.x / gridResolution)));
    grid.height = std::max(1, static_cast<int>(std::ceil(world.z / gridResolution)));
    grid.originXZ = {gridResolution * 0.5f, gridResolution * 0.5f};
    grid.maxXZ = {world.x, world.z};
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

            const glm::vec2 center = grid.cellCenter(gx, gz);
            const float wx = center.x;
            const float wz = center.y;
            const float wy = terrain.heightAtWorld(wx, wz) + 1.5f;
            const glm::vec3 rxPos{wx, wy, wz};

            float bestDesiredDbm = NEG_INF;
            float servingFrequencyMHz = 0.0f;
            int servingSourceIndex = -1;
            std::vector<Contribution> jammerContributions;
            jammerContributions.reserve(antennas.size());

            for (int antennaIndex = 0;
                 antennaIndex < static_cast<int>(antennas.size());
                 ++antennaIndex) {
                const auto& ant = antennas[antennaIndex];
                float distM = glm::distance(ant.position, rxPos);
                if (distM < 1.0f) distM = 1.0f;

                const float txPowerDbm = 10.0f * std::log10(
                    std::max(ant.powerW, 1.0e-9f) * 1000.0f);
                const float directionalGain = RFModels::effectiveAntennaGainDbi(
                    ant, rxPos, receiver);
                const float matLossDb = mat.lossDbPer100m(ant.frequencyMHz)
                                      * distM / 100.0f;
                float loss = fsplDb(distM, ant.frequencyMHz)
                           - directionalGain + matLossDb;

                if (!terrain.lineOfSight(ant.position, rxPos)) loss += 20.0f;
                const float rxDbm = txPowerDbm - loss;
                if (ant.isJammer) {
                    jammerContributions.push_back({
                        ant.frequencyMHz, RFModels::dbmToMw(rxDbm), antennaIndex});
                } else if (rxDbm > bestDesiredDbm) {
                    bestDesiredDbm = rxDbm;
                    servingFrequencyMHz = ant.frequencyMHz;
                    servingSourceIndex = antennaIndex;
                }
            }

            float jammerMw = 0.0f;
            float strongestJammerMw = 0.0f;
            int strongestJammerIndex = -1;
            const float halfBandwidthMHz = std::max(receiver.bandwidthHz, 1.0f)
                                         / 2.0e6f;
            for (const auto& contribution : jammerContributions) {
                if (!std::isfinite(bestDesiredDbm) ||
                    std::abs(contribution.frequencyMHz - servingFrequencyMHz) <= halfBandwidthMHz) {
                    jammerMw += contribution.powerMw;
                    if (contribution.powerMw > strongestJammerMw) {
                        strongestJammerMw = contribution.powerMw;
                        strongestJammerIndex = contribution.sourceIndex;
                    }
                }
            }

            const std::size_t index = static_cast<std::size_t>(gz) * grid.width + gx;
            grid.dbm[index] = bestDesiredDbm;
            grid.strongestDesiredDbm[index] = bestDesiredDbm;
            grid.jammerDbm[index] = RFModels::mwToDbm(jammerMw);
            grid.servingSourceIndex[index] = servingSourceIndex;
            grid.strongestJammerIndex[index] = strongestJammerIndex;
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
