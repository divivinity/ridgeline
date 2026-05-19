#pragma once
#include <string>
#include <vector>

struct AntennaType {
    std::string name;
    float defaultGainDbi = 3.0f;
    float beamwidthDeg   = 360.0f;  // 360 = omnidirectional
};

class AntennaLibrary {
public:
    // Load from JSON file; throws std::runtime_error on failure.
    void load(const std::string& path);

    // Populate from built-in defaults without reading any file.
    void loadDefaults();

    // All loaded types in order.
    const std::vector<AntennaType>& types() const { return m_types; }

    // Find by name; returns nullptr if not found.
    const AntennaType* find(const std::string& name) const;

    // Built-in fallback — four hard-coded types, no file needed.
    static const AntennaLibrary& defaults();

private:
    std::vector<AntennaType> m_types;
};
