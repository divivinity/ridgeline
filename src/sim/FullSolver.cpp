#include "FullSolver.h"
#include <cmath>
#include <complex>
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <glm/glm.hpp>

static constexpr float NEG_INF = -std::numeric_limits<float>::infinity();
static constexpr float PI      = 3.14159265f;

static float dBmToMw(float dbm) { return std::pow(10.0f, dbm / 10.0f); }
static float mwToDbm(float mw)  { return 10.0f * std::log10(mw); }

// ── FullSolver::solve ─────────────────────────────────────────────────────────

FieldGrid FullSolver::solve(const TerrainModel& terrain,
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
    if (progress) progress(0, grid.height);

    const Material& mat = materials.defaultMaterial();

    for (int gz = 0; gz < grid.height; ++gz) {
        if (cancelled && cancelled())
            throw std::runtime_error("Coverage solve cancelled");
        for (int gx = 0; gx < grid.width; ++gx) {
            if ((gx & 7) == 0 && cancelled && cancelled())
                throw std::runtime_error("Coverage solve cancelled");

            float wx = gx * gridResolution + gridResolution * 0.5f;
            float wz = gz * gridResolution + gridResolution * 0.5f;
            float wy = terrain.heightAtWorld(wx, wz) + 1.5f;  // 1.5 m receive height
            glm::vec3 rx{wx, wy, wz};

            float totalMw = 0.0f;

            for (const auto& ant : antennas) {
                if (ant.isJammer) continue;
                if (cancelled && cancelled())
                    throw std::runtime_error("Coverage solve cancelled");

                float distM = glm::distance(ant.position, rx);
                if (distM < 1.0f) distM = 1.0f;

                float txPowerDbm = 10.0f * std::log10(ant.powerW * 1000.0f);
                float eirpDbm    = txPowerDbm + ant.gainDbi;
                float freq       = ant.frequencyMHz;
                float distKm     = distM / 1000.0f;

                float matLossDb = mat.lossDbPer100m(freq) * distM / 100.0f;
                float rainDb    = enableRain
                                 ? rainAttenuationDb(rainRateMmH, freq, distKm)
                                 : 0.0f;

                // ── 1. Direct path ──────────────────────────────────────────
                auto losStatus = terrain.lineOfSightStatus(ant.position, rx);
                if (losStatus != TerrainModel::LOSStatus::Blocked) {
                    float directLoss = fsplDb(distM, freq) + matLossDb + rainDb;

                    if (enableFresnelPenalty) {
                        const float clearance =
                            terrain.fresnelClearanceRatio(ant.position, rx, freq);
                        if (clearance < requiredFresnelClearance) {
                            const float deficit =
                                std::clamp(requiredFresnelClearance - clearance,
                                           0.0f, 2.0f);
                            directLoss += deficit * 12.0f;
                        }
                    }

                    if (losStatus == TerrainModel::LOSStatus::Diffracted) {
                        auto obs = terrain.peakObstacle(ant.position, rx);
                        float d1 = std::max(distM * obs.t,             1.0f);
                        float d2 = std::max(distM * (1.0f - obs.t),    1.0f);
                        float lambda = 3.0e8f / (freq * 1.0e6f);
                        float nu = obs.clearanceM
                                 * std::sqrt(2.0f * distM / (lambda * d1 * d2));
                        directLoss += knifeEdgeLossDb(nu);
                    }

                    totalMw += dBmToMw(eirpDbm - directLoss);
                }

                // ── 2. Single-bounce ground reflection ──────────────────────
                if (enableGroundReflection) {
                    float mx = (ant.position.x + rx.x) * 0.5f;
                    float mz = (ant.position.z + rx.z) * 0.5f;
                    glm::vec3 sp{mx, terrain.heightAtWorld(mx, mz), mz};

                    float dRefl = glm::distance(ant.position, sp)
                                + glm::distance(sp, rx);
                    if (dRefl > 1.0f) {
                        glm::vec3 incoming  = glm::normalize(sp - ant.position);
                        float grazingAngle  = std::asin(std::abs(incoming.y));
                        grazingAngle = std::max(grazingAngle, 0.01f);

                        float refl = fresnelPowerRefl(mat.epsilon_r, mat.sigma_sm,
                                                       freq, grazingAngle);
                        if (refl > 0.001f) {
                            float reflLoss = fsplDb(dRefl, freq) + matLossDb + rainDb
                                           - 10.0f * std::log10(refl);
                            totalMw += dBmToMw(eirpDbm - reflLoss);
                        }
                    }
                }
            }

            if (totalMw > 0.0f)
                grid.dbm[gz * grid.width + gx] = mwToDbm(totalMw);
        }
        if (progress) progress(gz + 1, grid.height);
    }
    return grid;
}

// ── Static helpers ────────────────────────────────────────────────────────────

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

    float sin_g = std::sin(grazingAngleRad);
    float cos_g = std::cos(grazingAngleRad);

    C inner    = eps_c - C{cos_g * cos_g, 0.0f};
    C sqrtTerm = std::sqrt(inner);
    C num      = eps_c * C{sin_g, 0.0f} - sqrtTerm;
    C den      = eps_c * C{sin_g, 0.0f} + sqrtTerm;

    float gamma = (std::abs(den) > 1e-9f) ? (std::abs(num) / std::abs(den)) : 0.0f;
    return std::min(gamma * gamma, 1.0f);
}

float FullSolver::knifeEdgeLossDb(float nu) {
    // ITU-R P.526-15 piecewise approximation; returns positive dB of diffraction loss.
    if (nu <= -0.7f) return 0.0f;
    if (nu <= 0.0f)  return std::max(0.0f, -20.0f * std::log10(0.5f - 0.62f * nu));
    if (nu <= 1.0f)  return -20.0f * std::log10(0.5f * std::exp(-0.95f * nu));
    if (nu <= 2.4f) {
        float x     = 0.38f - 0.1f * nu;
        float inner = std::max(0.1184f - x * x, 0.0f);
        float arg   = 0.4f - std::sqrt(inner);
        return (arg > 0.0f) ? -20.0f * std::log10(arg) : 50.0f;
    }
    return -20.0f * std::log10(0.225f / std::max(nu, 0.001f));
}

float FullSolver::rainAttenuationDb(float rainRateMmH, float freqMHz, float distKm) {
    if (rainRateMmH <= 0.0f || distKm <= 0.0f) return 0.0f;

    // ITU-R P.838-3: k and alpha for vertical polarisation
    // { freq_MHz, k, alpha }
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
                float t = (freqMHz - tbl[i-1][0]) / (tbl[i][0] - tbl[i-1][0]);
                k     = tbl[i-1][1] + t * (tbl[i][1] - tbl[i-1][1]);
                alpha = tbl[i-1][2] + t * (tbl[i][2] - tbl[i-1][2]);
                break;
            }
        }
    }

    float specificAtten = k * std::pow(rainRateMmH, alpha);  // dB/km
    return specificAtten * distKm;
}
