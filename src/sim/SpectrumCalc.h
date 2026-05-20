#pragma once
#include <vector>

struct SpectrumPoint {
    float freqMHz;
    float powerDbm;
};

// Friis FSPL sweep. Computes received signal power at referenceDistM
// across a linear frequency sweep from freqStartMHz to freqEndMHz.
//   powerW         – transmit power in watts
//   gainDbi        – Tx antenna gain in dBi (Rx assumed 0 dBi)
//   referenceDistM – link distance in metres
//   freqStartMHz / freqEndMHz – inclusive sweep endpoints
//   steps          – number of sample points (>= 1)
// Throws std::invalid_argument if steps < 1.
std::vector<SpectrumPoint> sweepSpectrum(
    float powerW, float gainDbi,
    float referenceDistM,
    float freqStartMHz, float freqEndMHz, int steps);
