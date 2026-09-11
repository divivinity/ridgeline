#include "SolveSignature.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace {
// Fixed precision so the same inputs always produce the same text, and so a
// change too small to alter the result does not mark it stale.
void put(std::ostringstream& out, float value) {
    if (!std::isfinite(value)) { out << "nf;"; return; }
    out << std::fixed << std::setprecision(4) << value << ';';
}
void put(std::ostringstream& out, const std::string& value) { out << value << ';'; }
void put(std::ostringstream& out, bool value) { out << (value ? '1' : '0') << ';'; }
void put(std::ostringstream& out, int value) { out << value << ';'; }
}

std::string SolveSignature::of(const std::vector<Antenna>& antennas,
                               const ReceiverSettings& receiver,
                               SimEngine::Mode mode,
                               const CoverageCalibrationSettings& calibration,
                               const TerrainModel& terrain) {
    std::ostringstream out;
    out.imbue(std::locale::classic());

    put(out, static_cast<int>(mode));

    put(out, receiver.receiverHeightAglM);
    put(out, receiver.sensitivityDbm);
    put(out, receiver.bandwidthHz);
    put(out, receiver.noiseFigureDb);
    put(out, receiver.requiredSnrDb);
    put(out, receiver.fadeMarginTargetDb);
    put(out, receiver.polarization);

    put(out, calibration.enabled);
    put(out, calibration.desiredPowerOffsetDb);

    // Terrain identity, not its contents: a different grid or a different
    // georeference is a different scenario.
    put(out, terrain.width);
    put(out, terrain.height);
    put(out, terrain.metersPerCellX());
    put(out, terrain.metersPerCellZ());
    put(out, terrain.georeferenced);
    put(out, static_cast<float>(terrain.originLatDeg));
    put(out, static_cast<float>(terrain.originLonDeg));

    // Vegetation changes the result, so it belongs in the fingerprint.
    //
    // A rolling hash, not a sum: a sum cannot tell a stand of trees from the
    // same stand moved somewhere else, since both paint the same number of
    // samples at the same height. Moving vegetation changes coverage, so it has
    // to change the fingerprint.
    put(out, static_cast<int>(terrain.canopyHeightDm.size()));
    if (!terrain.canopyHeightDm.empty()) {
        std::uint64_t hash = 1469598103934665603ull;   // FNV-1a offset basis
        for (std::uint8_t v : terrain.canopyHeightDm) {
            hash ^= static_cast<std::uint64_t>(v);
            hash *= 1099511628211ull;                  // FNV prime
        }
        put(out, static_cast<int>(hash & 0x7fffffffu));
        put(out, static_cast<int>((hash >> 32) & 0x7fffffffu));
    }

    put(out, static_cast<int>(antennas.size()));
    for (const Antenna& a : antennas) {
        put(out, a.position.x); put(out, a.position.y); put(out, a.position.z);
        put(out, a.frequencyMHz);
        put(out, a.powerW);
        put(out, a.gainDbi);
        put(out, a.isJammer);
        put(out, a.antennaType);
        put(out, a.azimuthDeg);
        put(out, a.downtiltDeg);
        put(out, a.horizontalBeamwidthDeg);
        put(out, a.verticalBeamwidthDeg);
        put(out, a.frontToBackDb);
        put(out, a.cableLossDb);
        put(out, a.polarization);
        // Imported patterns change the result, so their contents count.
        put(out, static_cast<int>(a.azimuthPatternDb.size()));
        for (float v : a.azimuthPatternDb) put(out, v);
        put(out, static_cast<int>(a.elevationPatternDb.size()));
        for (float v : a.elevationPatternDb) put(out, v);
    }
    return out.str();
}
