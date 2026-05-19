#include "AntennaLibrary.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

void AntennaLibrary::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open antenna library: " + path);

    std::vector<AntennaType> tmp;
    try {
        json data = json::parse(f);
        for (const auto& entry : data["antenna_types"]) {
            AntennaType t;
            t.name           = entry["name"].get<std::string>();
            t.defaultGainDbi = entry["default_gain_dbi"].get<float>();
            t.beamwidthDeg   = entry["beamwidth_deg"].get<float>();
            tmp.push_back(std::move(t));
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("antennas.json parse error: ") + e.what());
    }
    m_types = std::move(tmp);
}

void AntennaLibrary::loadDefaults() {
    m_types = defaults().types();
}

const AntennaType* AntennaLibrary::find(const std::string& name) const {
    for (const auto& t : m_types)
        if (t.name == name) return &t;
    return nullptr;
}

const AntennaLibrary& AntennaLibrary::defaults() {
    static const AntennaLibrary lib = [] {
        AntennaLibrary l;
        l.m_types = {
            {"omni",   3.0f,  360.0f},
            {"dipole", 2.15f, 360.0f},
            {"yagi",   10.0f,  60.0f},
            {"patch",   8.0f,  90.0f},
        };
        return l;
    }();
    return lib;
}
