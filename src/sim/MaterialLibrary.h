#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct Material {
    std::string name;
    float epsilon_r = 3.0f;   // relative permittivity
    float sigma_sm  = 0.001f; // conductivity (S/m)

    struct BandLoss { float freqMHz; float lossDb; };
    std::vector<BandLoss> bandLoss; // loss dB per 100 m, sorted by freqMHz

    // Linear interpolation of loss dB/100m at the given frequency.
    // Clamps to nearest endpoint outside the table range.
    float lossDbPer100m(float freqMHz) const;
};

class MaterialLibrary {
public:
    // Parse assets/materials.json; throws std::runtime_error on failure.
    void load(const std::string& path);

    // Look up by name; returns defaultMaterial() if not found.
    const Material& find(const std::string& name) const;

    // Dry soil — available without loading any file.
    static const Material& defaultMaterial();

private:
    std::unordered_map<std::string, Material> m_materials;
};
