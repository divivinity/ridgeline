#include "RFModels.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
constexpr float kNegInf = -std::numeric_limits<float>::infinity();
constexpr float kPi = 3.14159265358979323846f;

float wrap180(float deg) {
    float v = std::fmod(deg + 180.0f, 360.0f);
    if (v < 0.0f) v += 360.0f;
    return v - 180.0f;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

float sampleCircularPattern(const std::vector<float>& pattern, float angleDeg) {
    if (pattern.empty()) return 0.0f;
    if (pattern.size() == 1) return std::max(0.0f, pattern.front());
    float a = std::fmod(angleDeg, 360.0f);
    if (a < 0.0f) a += 360.0f;
    const float pos = a / 360.0f * static_cast<float>(pattern.size());
    const int i0 = static_cast<int>(std::floor(pos)) % static_cast<int>(pattern.size());
    const int i1 = (i0 + 1) % static_cast<int>(pattern.size());
    const float t = pos - std::floor(pos);
    return std::max(0.0f, pattern[i0] * (1.0f - t) + pattern[i1] * t);
}

float sampleElevationPattern(const std::vector<float>& pattern, float elevationDeg) {
    if (pattern.empty()) return 0.0f;
    if (pattern.size() == 1) return std::max(0.0f, pattern.front());
    const float clamped = std::clamp(elevationDeg, -90.0f, 90.0f);
    const float pos = (clamped + 90.0f) / 180.0f
                    * static_cast<float>(pattern.size() - 1);
    const int i0 = static_cast<int>(std::floor(pos));
    const int i1 = std::min(i0 + 1, static_cast<int>(pattern.size() - 1));
    const float t = pos - static_cast<float>(i0);
    return std::max(0.0f, pattern[i0] * (1.0f - t) + pattern[i1] * t);
}
} // namespace

namespace RFModels {

void validateSolveInputs(const TerrainModel& terrain,
                         const std::vector<Antenna>& antennas,
                         const ReceiverSettings& receiver, float resolutionM) {
    terrain.validate();
    if (terrain.width < 2 || terrain.height < 2)
        throw std::invalid_argument("Coverage requires a complete terrain grid of at least 2 x 2 samples.");
    const glm::vec3 world = terrain.worldSize();
    if (!std::isfinite(world.x) || !std::isfinite(world.z) ||
        !std::isfinite(resolutionM) || resolutionM <= 0)
        throw std::invalid_argument("Coverage extent and grid resolution must be finite and positive.");
    const double columns = std::max(1.0, std::ceil(world.x / static_cast<double>(resolutionM)));
    const double rows = std::max(1.0, std::ceil(world.z / static_cast<double>(resolutionM)));
    if (columns * rows > std::numeric_limits<int>::max())
        throw std::invalid_argument("Coverage grid is too large. Increase the grid spacing or reduce the area.");
    if (!std::isfinite(receiver.receiverHeightAglM) || receiver.receiverHeightAglM < 0 ||
        !std::isfinite(receiver.bandwidthHz) || receiver.bandwidthHz <= 0 ||
        !std::isfinite(receiver.noiseFigureDb) || !std::isfinite(receiver.sensitivityDbm) ||
        !std::isfinite(receiver.requiredSnrDb) || !std::isfinite(receiver.fadeMarginTargetDb))
        throw std::invalid_argument("Receiver settings must be finite, with positive bandwidth and nonnegative height.");
    for (std::size_t i = 0; i < antennas.size(); ++i) {
        const auto& a = antennas[i];
        const std::string prefix = "Source " + std::to_string(i + 1) + ": ";
        if (!std::isfinite(a.position.x) || !std::isfinite(a.position.y) || !std::isfinite(a.position.z))
            throw std::invalid_argument(prefix + "position must be finite.");
        if (a.position.x < 0 || a.position.z < 0 || a.position.x > world.x || a.position.z > world.z)
            throw std::invalid_argument(prefix + "outside analysis area. Choose terrain containing the source or reposition it.");
        if (!std::isfinite(a.frequencyMHz) || a.frequencyMHz <= 0 ||
            !std::isfinite(a.powerW) || a.powerW <= 0 ||
            !std::isfinite(a.gainDbi) || !std::isfinite(a.cableLossDb) ||
            !std::isfinite(a.azimuthDeg) || !std::isfinite(a.downtiltDeg) ||
            !std::isfinite(a.horizontalBeamwidthDeg) || !std::isfinite(a.verticalBeamwidthDeg) ||
            !std::isfinite(a.frontToBackDb))
            throw std::invalid_argument(prefix + "RF settings must be finite, with positive frequency and power.");
        for (const auto* pattern : {&a.azimuthPatternDb, &a.elevationPatternDb})
            if (std::any_of(pattern->begin(), pattern->end(), [](float v) { return !std::isfinite(v); }))
                throw std::invalid_argument(prefix + "antenna pattern contains nonfinite values.");
    }
}

float dbmToMw(float dbm) {
    if (!std::isfinite(dbm)) return 0.0f;
    return std::pow(10.0f, dbm / 10.0f);
}

float mwToDbm(float mw) {
    return mw > 0.0f ? 10.0f * std::log10(mw) : kNegInf;
}

float noiseFloorDbm(const ReceiverSettings& receiver) {
    const float bw = std::max(receiver.bandwidthHz, 1.0f);
    return -174.0f + 10.0f * std::log10(bw) + receiver.noiseFigureDb;
}

float polarizationMismatchLossDb(const std::string& txPolarization,
                                 const std::string& rxPolarization) {
    const std::string tx = lower(txPolarization);
    const std::string rx = lower(rxPolarization);
    if (tx.empty() || rx.empty() || tx == rx) return 0.0f;

    const bool txCircular = tx.find("circular") != std::string::npos ||
                            tx == "rhcp" || tx == "lhcp";
    const bool rxCircular = rx.find("circular") != std::string::npos ||
                            rx == "rhcp" || rx == "lhcp";
    if (txCircular != rxCircular) return 3.0f;
    if (txCircular && rxCircular) return tx == rx ? 0.0f : 20.0f;

    const bool txH = tx.find("horizontal") != std::string::npos || tx == "h";
    const bool rxH = rx.find("horizontal") != std::string::npos || rx == "h";
    const bool txV = tx.find("vertical") != std::string::npos || tx == "v";
    const bool rxV = rx.find("vertical") != std::string::npos || rx == "v";
    if ((txH && rxV) || (txV && rxH)) return 20.0f;
    return 3.0f;
}

float effectiveAntennaGainDbi(const Antenna& antenna,
                              const glm::vec3& target,
                              const ReceiverSettings& receiver) {
    const glm::vec3 delta = target - antenna.position;
    const float horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    const float bearingDeg = std::atan2(delta.x, delta.z) * 180.0f / kPi;
    const float elevationDeg = std::atan2(delta.y, std::max(horizontal, 0.001f))
                             * 180.0f / kPi;

    const float relativeAz = wrap180(bearingDeg - antenna.azimuthDeg);
    const float boresightElevation = -antenna.downtiltDeg;
    const float relativeEl = elevationDeg - boresightElevation;

    float horizontalAtten = 0.0f;
    if (!antenna.azimuthPatternDb.empty()) {
        horizontalAtten = sampleCircularPattern(
            antenna.azimuthPatternDb, relativeAz < 0.0f ? relativeAz + 360.0f : relativeAz);
    } else if (antenna.horizontalBeamwidthDeg < 359.0f) {
        const float bw = std::max(antenna.horizontalBeamwidthDeg, 1.0f);
        horizontalAtten = std::min(
            12.0f * std::pow(relativeAz / bw, 2.0f),
            std::max(antenna.frontToBackDb, 0.0f));
    }

    float verticalAtten = 0.0f;
    if (!antenna.elevationPatternDb.empty()) {
        verticalAtten = sampleElevationPattern(antenna.elevationPatternDb, relativeEl);
    } else {
        const float bw = std::max(antenna.verticalBeamwidthDeg, 1.0f);
        verticalAtten = std::min(12.0f * std::pow(relativeEl / bw, 2.0f), 30.0f);
    }

    const float polLoss = polarizationMismatchLossDb(
        antenna.polarization, receiver.polarization);
    return antenna.gainDbi
         - horizontalAtten
         - verticalAtten
         - std::max(0.0f, antenna.cableLossDb)
         - polLoss;
}

void initializeMetrics(FieldGrid& grid) {
    const std::size_t n = grid.cellCount();
    grid.jammerDbm.assign(n, kNegInf);
    grid.snrDb.assign(n, kNegInf);
    grid.sinrDb.assign(n, kNegInf);
    grid.jOverSDb.assign(n, kNegInf);
    grid.fadeMarginDb.assign(n, kNegInf);
    grid.strongestDesiredDbm.assign(n, kNegInf);
    grid.service.assign(n, 0);
    grid.servingSourceIndex.assign(n, -1);
    grid.strongestJammerIndex.assign(n, -1);
}

void finalizeMetrics(FieldGrid& grid, const ReceiverSettings& receiver) {
    if (grid.jammerDbm.size() != grid.dbm.size()) initializeMetrics(grid);
    grid.noiseFloorDbm = noiseFloorDbm(receiver);
    grid.receiverSensitivityDbm = receiver.sensitivityDbm;
    grid.requiredSnrDb = receiver.requiredSnrDb;
    const float noiseMw = dbmToMw(grid.noiseFloorDbm);

    for (std::size_t i = 0; i < grid.dbm.size(); ++i) {
        const float desired = grid.dbm[i];
        const float jammer = grid.jammerDbm[i];
        if (!std::isfinite(desired)) continue;

        grid.snrDb[i] = desired - grid.noiseFloorDbm;
        const float interferenceMw = noiseMw + dbmToMw(jammer);
        grid.sinrDb[i] = desired - mwToDbm(interferenceMw);
        grid.jOverSDb[i] = std::isfinite(jammer) ? jammer - desired : kNegInf;
        grid.fadeMarginDb[i] = desired - receiver.sensitivityDbm;
        grid.service[i] = static_cast<std::uint8_t>(
            desired >= receiver.sensitivityDbm &&
            grid.sinrDb[i] >= receiver.requiredSnrDb &&
            grid.fadeMarginDb[i] >= receiver.fadeMarginTargetDb);
    }
}

} // namespace RFModels
