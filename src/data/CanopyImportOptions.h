#pragma once

#include "LandfireCanopy.h"
#include "ScenarioIO.h"
#include <cmath>

// A reload observes the proposed options, but they become persistent only if
// the replacement was installed. Failure, cancellation and exceptions roll back.
template<class Reload>
bool replaceCanopyOptions(landfire::Options& current,
                          const landfire::Options& proposed, Reload reload) {
    const auto previous = current;
    current = proposed;
    try {
        if (reload()) return true;
    } catch (...) {
        current = previous;
        throw;
    }
    current = previous;
    return false;
}

// Reconstruct independently of the previously open scenario. Invalid heights
// fall back to the documented default; zero is a valid UI-supported assumption.
inline landfire::Options canopyOptionsForImport(const CanopyImport& record) {
    landfire::Options options;
    if (record.developedPolicy == "source")
        options.developed = landfire::DevelopedPolicy::UseSourceValue;
    else if (record.developedPolicy == "bare")
        options.developed = landfire::DevelopedPolicy::TreatAsBare;
    const float height = record.assumedDevelopedCanopyM;
    if (std::isfinite(height) && height >= 0.0f && height <= 60.0f)
        options.assumedDevelopedCanopyM = height;
    return options;
}
