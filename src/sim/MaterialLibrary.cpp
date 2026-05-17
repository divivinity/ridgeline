#include "MaterialLibrary.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <algorithm>

using json = nlohmann::json;

float Material::lossDbPer100m(float freqMHz) const {
    if (bandLoss.empty()) return 0.0f;
    if (bandLoss.size() == 1) return bandLoss[0].lossDb;

    if (freqMHz <= bandLoss.front().freqMHz) return bandLoss.front().lossDb;
    if (freqMHz >= bandLoss.back().freqMHz)  return bandLoss.back().lossDb;

    for (size_t i = 1; i < bandLoss.size(); ++i) {
        if (freqMHz <= bandLoss[i].freqMHz) {
            float t = (freqMHz - bandLoss[i-1].freqMHz)
                    / (bandLoss[i].freqMHz - bandLoss[i-1].freqMHz);
            return bandLoss[i-1].lossDb + t * (bandLoss[i].lossDb - bandLoss[i-1].lossDb);
        }
    }
    return bandLoss.back().lossDb;
}

void MaterialLibrary::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open materials file: " + path);

    json data = json::parse(f);
    for (const auto& entry : data["materials"]) {
        Material mat;
        mat.name      = entry["name"].get<std::string>();
        mat.epsilon_r = entry["epsilon_r"].get<float>();
        mat.sigma_sm  = entry["sigma_sm"].get<float>();
        for (const auto& b : entry["band_loss"]) {
            mat.bandLoss.push_back({
                b["freq_mhz"].get<float>(),
                b["loss_db_per_100m"].get<float>()
            });
        }
        std::sort(mat.bandLoss.begin(), mat.bandLoss.end(),
                  [](const auto& a, const auto& b){ return a.freqMHz < b.freqMHz; });
        m_materials[mat.name] = std::move(mat);
    }
}

const Material& MaterialLibrary::find(const std::string& name) const {
    auto it = m_materials.find(name);
    return (it != m_materials.end()) ? it->second : defaultMaterial();
}

const Material& MaterialLibrary::defaultMaterial() {
    static const Material m = [] {
        Material mat;
        mat.name      = "dry_soil";
        mat.epsilon_r = 3.0f;
        mat.sigma_sm  = 0.001f;
        mat.bandLoss  = {{100.0f,  0.3f},
                         {300.0f,  0.5f},
                         {1000.0f, 1.2f},
                         {3000.0f, 3.0f}};
        return mat;
    }();
    return m;
}
