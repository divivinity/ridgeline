#include "SpectrumCalc.h"
#include <cmath>
#include <stdexcept>

std::vector<SpectrumPoint> sweepSpectrum(
    float powerW, float gainDbi,
    float referenceDistM,
    float freqStartMHz, float freqEndMHz, int steps)
{
    if (steps < 1) throw std::invalid_argument("steps must be >= 1");
    if (referenceDistM <= 0.0f) throw std::invalid_argument("referenceDistM must be > 0");
    if (powerW <= 0.0f)         throw std::invalid_argument("powerW must be > 0");

    // P_tx in dBm
    const float txDbm = 10.0f * std::log10(powerW * 1000.0f);

    // Friis FSPL (dB) = 20·log10(4π/c) + 20·log10(d_m) + 20·log10(f_Hz)
    // 20·log10(4π / 3×10⁸) ≈ −147.558
    const float K        = 20.0f * std::log10(4.0f * 3.14159265f / 3.0e8f);
    const float distTerm = 20.0f * std::log10(referenceDistM);

    std::vector<SpectrumPoint> out;
    out.reserve(steps);

    for (int i = 0; i < steps; ++i) {
        const float t    = (steps == 1) ? 0.0f
                                        : static_cast<float>(i) / (steps - 1);
        const float fMHz = freqStartMHz + t * (freqEndMHz - freqStartMHz);
        const float fHz  = fMHz * 1.0e6f;

        const float fspl  = K + distTerm + 20.0f * std::log10(fHz);
        const float rxDbm = txDbm + gainDbi - fspl;
        out.push_back({ fMHz, rxDbm });
    }
    return out;
}
