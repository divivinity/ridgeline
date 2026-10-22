#pragma once
#include <nlohmann/json.hpp>

#include "sim/Types.h"

// The equipment half of a source: everything about the radio and antenna, and
// nothing about where it stands or what role it plays. Scenarios write these
// keys alongside position and role; equipment presets write only these. Both
// go through here so the two can never disagree about what equipment is.
namespace AntennaJson {

inline void writeEquipment(nlohmann::json& j, const Antenna& a) {
    j["freq_mhz"] = a.frequencyMHz;
    j["power_w"] = a.powerW;
    j["gain_dbi"] = a.gainDbi;
    j["antenna_type"] = a.antennaType;
    j["azimuth_deg"] = a.azimuthDeg;
    j["downtilt_deg"] = a.downtiltDeg;
    j["horizontal_beamwidth_deg"] = a.horizontalBeamwidthDeg;
    j["vertical_beamwidth_deg"] = a.verticalBeamwidthDeg;
    j["front_to_back_db"] = a.frontToBackDb;
    j["cable_loss_db"] = a.cableLossDb;
    j["polarization"] = a.polarization;
    if (!a.azimuthPatternDb.empty()) j["azimuth_pattern_db"] = a.azimuthPatternDb;
    if (!a.elevationPatternDb.empty()) j["elevation_pattern_db"] = a.elevationPatternDb;
    if (!a.patternSource.empty()) j["pattern_source"] = a.patternSource;
}

// Every equipment field is assigned, absent keys taking their defaults, so
// reading onto an existing source replaces its equipment wholesale: a preset
// without a measured pattern removes the target's pattern rather than leaving
// it behind.
inline void readEquipment(const nlohmann::json& j, Antenna& a) {
    const Antenna defaults{};
    a.frequencyMHz = j.value("freq_mhz", defaults.frequencyMHz);
    a.powerW = j.value("power_w", defaults.powerW);
    a.gainDbi = j.value("gain_dbi", defaults.gainDbi);
    a.antennaType = j.value("antenna_type", defaults.antennaType);
    a.azimuthDeg = j.value("azimuth_deg", defaults.azimuthDeg);
    a.downtiltDeg = j.value("downtilt_deg", defaults.downtiltDeg);
    a.horizontalBeamwidthDeg =
        j.value("horizontal_beamwidth_deg", defaults.horizontalBeamwidthDeg);
    a.verticalBeamwidthDeg = j.value("vertical_beamwidth_deg", defaults.verticalBeamwidthDeg);
    a.frontToBackDb = j.value("front_to_back_db", defaults.frontToBackDb);
    a.cableLossDb = j.value("cable_loss_db", defaults.cableLossDb);
    a.polarization = j.value("polarization", defaults.polarization);
    a.azimuthPatternDb = j.contains("azimuth_pattern_db")
        ? j["azimuth_pattern_db"].get<std::vector<float>>() : std::vector<float>{};
    a.elevationPatternDb = j.contains("elevation_pattern_db")
        ? j["elevation_pattern_db"].get<std::vector<float>>() : std::vector<float>{};
    a.patternSource = j.value("pattern_source", std::string{});
}

// `target` with `equipment`'s radio and antenna, keeping its own position,
// height above ground and role.
inline Antenna withEquipment(Antenna target, const Antenna& equipment) {
    nlohmann::json j;
    writeEquipment(j, equipment);
    readEquipment(j, target);
    return target;
}

} // namespace AntennaJson
