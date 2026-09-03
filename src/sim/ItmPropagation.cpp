#include "ItmPropagation.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef HAVE_NTIA_ITM
extern "C" int ITM_P2P_CR(double h_tx__meter, double h_rx__meter, double pfl[],
                           int climate, double N_0, double f__mhz, int pol,
                           double epsilon, double sigma, int mdvar,
                           double confidence, double reliability,
                           double* A__db, long* warnings);
#endif

ItmPrediction ItmPropagation::predict(const TerrainModel& terrain,
                                      glm::vec3 tx,
                                      glm::vec3 rx,
                                      const Antenna& antenna,
                                      const Material& material) {
    ItmPrediction prediction;
#ifndef HAVE_NTIA_ITM
    (void)terrain; (void)tx; (void)rx; (void)antenna; (void)material;
    return prediction;
#else
    const float horizontalM = glm::distance(glm::vec2(tx.x, tx.z),
                                            glm::vec2(rx.x, rx.z));
    if (horizontalM < 1000.0f || antenna.frequencyMHz < 20.0f ||
        antenna.frequencyMHz > 20000.0f) {
        return prediction;
    }

    const float terrainStep = std::max(
        5.0f, std::min(terrain.metersPerCellX(), terrain.metersPerCellZ()));
    const int points = std::clamp(
        static_cast<int>(std::ceil(horizontalM / std::max(terrainStep, 30.0f))) + 1,
        33, 257);
    const double spacing = horizontalM / static_cast<double>(points - 1);

    // NTIA PFL layout: [point_count-1, spacing_m, elevation_0, ...].
    std::vector<double> pfl(static_cast<std::size_t>(points) + 2);
    pfl[0] = static_cast<double>(points - 1);
    pfl[1] = spacing;
    for (int i = 0; i < points; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(points - 1);
        const float x = tx.x + (rx.x - tx.x) * t;
        const float z = tx.z + (rx.z - tx.z) * t;
        pfl[static_cast<std::size_t>(i) + 2] = terrain.heightAtWorld(x, z);
    }

    const double txGround = terrain.heightAtWorld(tx.x, tx.z);
    const double rxGround = terrain.heightAtWorld(rx.x, rx.z);
    const double txAgl = std::clamp(static_cast<double>(tx.y) - txGround, 0.5, 3000.0);
    const double rxAgl = std::clamp(static_cast<double>(rx.y) - rxGround, 0.5, 3000.0);

    const std::string pol = antenna.polarization;
    const bool horizontal = pol == "horizontal" || pol == "Horizontal" || pol == "h";
    const int polarization = horizontal ? 0 : 1;

    double lossDb = 0.0;
    long warnings = 0;
    // Continental-temperate, standard refractivity, broadcast variability,
    // median confidence/reliability. These are intentionally conservative
    // general-purpose defaults until per-scenario environmental controls exist.
    const int error = ITM_P2P_CR(
        txAgl, rxAgl, pfl.data(),
        5, 301.0, antenna.frequencyMHz, polarization,
        std::max(1.01f, material.epsilon_r),
        std::max(1.0e-6f, material.sigma_sm),
        3, 50.0, 50.0, &lossDb, &warnings);

    prediction.errorCode = error;
    prediction.warnings = warnings;
    prediction.valid = error == 0 && std::isfinite(lossDb) && lossDb > 0.0;
    prediction.basicTransmissionLossDb = static_cast<float>(lossDb);
    return prediction;
#endif
}
