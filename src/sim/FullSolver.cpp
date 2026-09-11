#include "FullSolver.h"
#include "RFModels.h"
#include "ItmPropagation.h"
#include <cmath>
#include <complex>
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

static constexpr float NEG_INF = -std::numeric_limits<float>::infinity();
static constexpr float PI      = 3.14159265f;
static constexpr float EFFECTIVE_EARTH_RADIUS_M = 6371000.0f * 4.0f / 3.0f;

namespace {

float earthBulge(float pathM, float t) {
    const float d1 = pathM * t;
    const float d2 = pathM * (1.0f - t);
    return (d1 * d2) / (2.0f * EFFECTIVE_EARTH_RADIUS_M);
}

struct EdgeCandidate {
    float excessM = 0.0f;
    float t = 0.5f;
    float lossDb = 0.0f;
};

struct Contribution {
    float frequencyMHz = 0.0f;
    float powerMw = 0.0f;
    int sourceIndex = -1;
};

} // namespace

FieldGrid FullSolver::solve(const TerrainModel& terrain,
                             const std::vector<Antenna>& antennas,
                             const MaterialLibrary& materials,
                             const std::function<void(int, int)>& progress,
                             const std::function<bool()>& cancelled) const {
    RFModels::validateSolveInputs(terrain, antennas, receiver, gridResolution);
    FieldGrid grid;
    grid.cellSize = gridResolution;
    const glm::vec3 world = terrain.worldSize();
    grid.width    = std::max(1, static_cast<int>(std::ceil(world.x / gridResolution)));
    grid.height   = std::max(1, static_cast<int>(std::ceil(world.z / gridResolution)));
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
            if ((gx & 7) == 0 && cancelled && cancelled())
                throw std::runtime_error("Coverage solve cancelled");

            const glm::vec2 center = grid.cellCenter(gx, gz);
            const float wx = center.x;
            const float wz = center.y;
            const float wy = terrain.heightAtWorld(wx, wz)
                           + receiver.receiverHeightAglM;
            const glm::vec3 rx{wx, wy, wz};

            float strongestDesiredDbm = NEG_INF;
            float servingFrequencyMHz = 0.0f;
            int servingSourceIndex = -1;
            std::vector<Contribution> desiredContributions;
            std::vector<Contribution> jammerContributions;
            desiredContributions.reserve(antennas.size());
            jammerContributions.reserve(antennas.size());

            for (int antennaIndex = 0;
                 antennaIndex < static_cast<int>(antennas.size());
                 ++antennaIndex) {
                const auto& ant = antennas[antennaIndex];
                if (cancelled && cancelled())
                    throw std::runtime_error("Coverage solve cancelled");

                float distM = glm::distance(ant.position, rx);
                if (distM < 1.0f) distM = 1.0f;
                const float freq = ant.frequencyMHz;
                const float distKm = distM / 1000.0f;
                const float txPowerDbm = 10.0f * std::log10(
                    std::max(ant.powerW, 1.0e-9f) * 1000.0f);
                const float effectiveGainDbi = RFModels::effectiveAntennaGainDbi(
                    ant, rx, receiver);
                const float rainDb = enableRain
                    ? rainAttenuationDb(rainRateMmH, freq, distKm) : 0.0f;
                // Vegetation rides alongside rain rather than sitting in the
                // analytic branch. Rain is the existing precedent for an excess
                // term applied to both branches; a foliage term added only to
                // the analytic side would be silently inert in Engineering mode
                // beyond 1 km, where ITM takes over. ITM models no land cover -
                // its clutter factor is bare-earth roughness, diffraction-region
                // only and capped at 15 dB - so this is additive, not double
                // counting.
                const float foliageDb = terrain.hasVegetation()
                    ? RFModels::vegetationLossDb(
                          terrain.foliageDepthM(ant.position, rx), freq)
                    : 0.0f;

                float directRxDbm = NEG_INF;
                const float itmLoss = enableItm
                    ? itmBasicTransmissionLossDb(
                        terrain, ant.position, rx, freq, mat, ant)
                    : std::numeric_limits<float>::quiet_NaN();

                if (std::isfinite(itmLoss)) {
                    directRxDbm = txPowerDbm + effectiveGainDbi
                                - itmLoss - rainDb - foliageDb;
                } else {
                    const float matLossDb = mat.lossDbPer100m(freq) * distM / 100.0f;
                    float directLoss = fsplDb(distM, freq) + matLossDb + rainDb + foliageDb;
                    const auto losStatus = terrain.lineOfSightStatus(ant.position, rx);

                    if (enableFresnelPenalty && losStatus == TerrainModel::LOSStatus::Clear) {
                        const float clearance = terrain.fresnelClearanceRatio(
                            ant.position, rx, freq);
                        if (clearance < requiredFresnelClearance) {
                            const float deficit = std::clamp(
                                requiredFresnelClearance - clearance, 0.0f, 2.0f);
                            directLoss += deficit * 12.0f;
                        }
                    }

                    if (losStatus == TerrainModel::LOSStatus::Diffracted) {
                        const auto obs = terrain.peakObstacle(ant.position, rx);
                        const float d1 = std::max(distM * obs.t, 1.0f);
                        const float d2 = std::max(distM * (1.0f - obs.t), 1.0f);
                        const float lambda = 3.0e8f / (freq * 1.0e6f);
                        const float nu = obs.clearanceM
                            * std::sqrt(2.0f * distM / (lambda * d1 * d2));
                        directLoss += knifeEdgeLossDb(nu);
                    } else if (losStatus == TerrainModel::LOSStatus::Blocked) {
                        if (enableMultipleDiffraction) {
                            directLoss += multipleKnifeEdgeLossDb(
                                terrain, ant.position, rx, freq);
                        } else {
                            directLoss += 35.0f;
                        }
                    }
                    directRxDbm = txPowerDbm + effectiveGainDbi - directLoss;
                }

                float pathMw = RFModels::dbmToMw(directRxDbm);

                // A reflected component is retained for short/medium paths when
                // ITM is not being used. ITM already models ground effects as
                // part of its median basic transmission loss.
                if (enableGroundReflection && !std::isfinite(itmLoss)) {
                    const float mx = (ant.position.x + rx.x) * 0.5f;
                    const float mz = (ant.position.z + rx.z) * 0.5f;
                    const glm::vec3 sp{mx, terrain.heightAtWorld(mx, mz), mz};
                    const float dRefl = glm::distance(ant.position, sp)
                                      + glm::distance(sp, rx);
                    if (dRefl > 1.0f) {
                        const glm::vec3 incoming = glm::normalize(sp - ant.position);
                        const float grazingAngle = std::max(
                            std::asin(std::abs(incoming.y)), 0.01f);
                        const float refl = fresnelPowerRefl(
                            mat.epsilon_r, mat.sigma_sm, freq, grazingAngle);
                        if (refl > 0.001f) {
                            const float reflMatLoss = mat.lossDbPer100m(freq)
                                                   * dRefl / 100.0f;
                            const float reflLoss = fsplDb(dRefl, freq)
                                + reflMatLoss + rainDb
                                - 10.0f * std::log10(refl);
                            pathMw += RFModels::dbmToMw(
                                txPowerDbm + effectiveGainDbi - reflLoss);
                        }
                    }
                }

                if (ant.isJammer) {
                    jammerContributions.push_back({freq, pathMw, antennaIndex});
                } else {
                    desiredContributions.push_back({freq, pathMw, antennaIndex});
                    const float pathDbm = RFModels::mwToDbm(pathMw);
                    if (pathDbm > strongestDesiredDbm) {
                        strongestDesiredDbm = pathDbm;
                        servingFrequencyMHz = freq;
                        servingSourceIndex = antennaIndex;
                    }
                }
            }

            float desiredMw = 0.0f;
            float jammerMw = 0.0f;
            float strongestJammerMw = 0.0f;
            int strongestJammerIndex = -1;
            const float halfBandwidthMHz = std::max(receiver.bandwidthHz, 1.0f)
                                         / 2.0e6f;
            for (const auto& contribution : desiredContributions) {
                if (!std::isfinite(strongestDesiredDbm) ||
                    std::abs(contribution.frequencyMHz - servingFrequencyMHz) <= halfBandwidthMHz) {
                    desiredMw += contribution.powerMw;
                }
            }
            for (const auto& contribution : jammerContributions) {
                if (!std::isfinite(strongestDesiredDbm) ||
                    std::abs(contribution.frequencyMHz - servingFrequencyMHz) <= halfBandwidthMHz) {
                    jammerMw += contribution.powerMw;
                    if (contribution.powerMw > strongestJammerMw) {
                        strongestJammerMw = contribution.powerMw;
                        strongestJammerIndex = contribution.sourceIndex;
                    }
                }
            }

            const std::size_t index = static_cast<std::size_t>(gz) * grid.width + gx;
            grid.dbm[index] = RFModels::mwToDbm(desiredMw);
            grid.strongestDesiredDbm[index] = strongestDesiredDbm;
            grid.jammerDbm[index] = RFModels::mwToDbm(jammerMw);
            grid.servingSourceIndex[index] = servingSourceIndex;
            grid.strongestJammerIndex[index] = strongestJammerIndex;
        }
        if (progress) progress(gz + 1, grid.height);
    }

    RFModels::finalizeMetrics(grid, receiver);
    return grid;
}

float FullSolver::fsplDb(float distM, float freqMHz) {
    return 20.0f * std::log10(distM)
         + 20.0f * std::log10(freqMHz * 1.0e6f)
         - 147.55f;
}

float FullSolver::fresnelPowerRefl(float eps_r, float sigma_sm,
                                    float freqMHz, float grazingAngleRad) {
    using C = std::complex<float>;
    const float omega = 2.0f * PI * freqMHz * 1.0e6f;
    const float eps0  = 8.854e-12f;
    C eps_c{eps_r, -sigma_sm / (omega * eps0)};

    const float sin_g = std::sin(grazingAngleRad);
    const float cos_g = std::cos(grazingAngleRad);
    C inner = eps_c - C{cos_g * cos_g, 0.0f};
    C sqrtTerm = std::sqrt(inner);
    C num = eps_c * C{sin_g, 0.0f} - sqrtTerm;
    C den = eps_c * C{sin_g, 0.0f} + sqrtTerm;

    const float gamma = (std::abs(den) > 1e-9f)
        ? (std::abs(num) / std::abs(den)) : 0.0f;
    return std::min(gamma * gamma, 1.0f);
}

float FullSolver::knifeEdgeLossDb(float nu) {
    if (nu <= -0.7f) return 0.0f;
    if (nu <= 0.0f)
        return std::max(0.0f, -20.0f * std::log10(0.5f - 0.62f * nu));
    if (nu <= 1.0f)
        return -20.0f * std::log10(0.5f * std::exp(-0.95f * nu));
    if (nu <= 2.4f) {
        const float x = 0.38f - 0.1f * nu;
        const float inner = std::max(0.1184f - x * x, 0.0f);
        const float arg = 0.4f - std::sqrt(inner);
        return (arg > 0.0f) ? -20.0f * std::log10(arg) : 50.0f;
    }
    return -20.0f * std::log10(0.225f / std::max(nu, 0.001f));
}

float FullSolver::multipleKnifeEdgeLossDb(const TerrainModel& terrain,
                                           glm::vec3 from,
                                           glm::vec3 to,
                                           float frequencyMHz) {
    if (frequencyMHz <= 0.0f) return 0.0f;
    const int steps = terrain.adaptivePathSteps(from, to, 64, 1024);
    const float horizontalPathM = glm::distance(
        glm::vec2(from.x, from.z), glm::vec2(to.x, to.z));
    if (horizontalPathM <= 1.0f) return 0.0f;

    const glm::vec3 dir = to - from;
    const float lambda = 3.0e8f / (frequencyMHz * 1.0e6f);
    std::vector<EdgeCandidate> edges;
    edges.reserve(16);

    float previousExcess = 0.0f;
    float currentExcess = 0.0f;
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const glm::vec3 p = from + dir * t;
        const float losHeight = p.y;
        const float terrainHeight = terrain.heightAtWorld(p.x, p.z)
                                  + earthBulge(horizontalPathM, t);
        const float nextT = static_cast<float>(i + 1) / static_cast<float>(steps);
        const glm::vec3 nextP = from + dir * std::min(nextT, 1.0f);
        const float nextExcess = i + 1 < steps
            ? terrain.heightAtWorld(nextP.x, nextP.z)
                + earthBulge(horizontalPathM, nextT) - nextP.y
            : 0.0f;
        currentExcess = terrainHeight - losHeight;

        if (currentExcess > 0.0f && currentExcess >= previousExcess &&
            currentExcess >= nextExcess) {
            const float d1 = std::max(horizontalPathM * t, 1.0f);
            const float d2 = std::max(horizontalPathM * (1.0f - t), 1.0f);
            const float nu = currentExcess
                * std::sqrt(2.0f * horizontalPathM / (lambda * d1 * d2));
            edges.push_back({currentExcess, t, knifeEdgeLossDb(nu)});
        }
        previousExcess = currentExcess;
    }

    if (edges.empty()) return 0.0f;
    std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
        return a.lossDb > b.lossDb;
    });

    // Deygout-style dominant-edge approximation: the strongest obstruction is
    // applied in full, with secondary separated ridges contributing a reduced
    // correction. This is substantially more realistic than treating any
    // multi-obstacle path as an all-or-nothing block.
    float loss = edges.front().lossDb;
    float lastT = edges.front().t;
    int secondary = 0;
    for (std::size_t i = 1; i < edges.size() && secondary < 4; ++i) {
        if (std::abs(edges[i].t - lastT) < 0.03f) continue;
        loss += 0.5f * edges[i].lossDb;
        lastT = edges[i].t;
        ++secondary;
    }
    return std::clamp(loss, 0.0f, 80.0f);
}

float FullSolver::itmBasicTransmissionLossDb(const TerrainModel& terrain,
                                              glm::vec3 from,
                                              glm::vec3 to,
                                              float frequencyMHz,
                                              const Material& material,
                                              const Antenna& antenna) {
    Antenna itmAntenna = antenna;
    itmAntenna.frequencyMHz = frequencyMHz;
    const ItmPrediction prediction = ItmPropagation::predict(
        terrain, from, to, itmAntenna, material);
    return prediction.valid
        ? prediction.basicTransmissionLossDb
        : std::numeric_limits<float>::quiet_NaN();
}

float FullSolver::rainAttenuationDb(float rainRateMmH, float freqMHz, float distKm) {
    if (rainRateMmH <= 0.0f || distKm <= 0.0f) return 0.0f;

    static const float tbl[][3] = {
        {100.0f,    0.0000387f, 0.912f},
        {300.0f,    0.000354f,  0.962f},
        {1000.0f,   0.00387f,   1.000f},
        {3000.0f,   0.0178f,    1.127f},
        {10000.0f,  0.101f,     1.276f},
    };
    static const int N = 5;

    float k = tbl[0][1], alpha = tbl[0][2];
    if (freqMHz <= tbl[0][0]) {
        k = tbl[0][1]; alpha = tbl[0][2];
    } else if (freqMHz >= tbl[N-1][0]) {
        k = tbl[N-1][1]; alpha = tbl[N-1][2];
    } else {
        for (int i = 1; i < N; ++i) {
            if (freqMHz <= tbl[i][0]) {
                const float t = (freqMHz - tbl[i-1][0])
                              / (tbl[i][0] - tbl[i-1][0]);
                k = tbl[i-1][1] + t * (tbl[i][1] - tbl[i-1][1]);
                alpha = tbl[i-1][2] + t * (tbl[i][2] - tbl[i-1][2]);
                break;
            }
        }
    }

    return k * std::pow(rainRateMmH, alpha) * distKm;
}
