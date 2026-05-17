#include "SimEngine.h"
#include <stdexcept>

void SimEngine::setTerrain(TerrainModel terrain) {
    m_terrain = std::move(terrain);
}

void SimEngine::setAntennas(std::vector<Antenna> antennas) {
    m_antennas = std::move(antennas);
}

void SimEngine::loadMaterials(const std::string& path) {
    try {
        m_materials.load(path);
    } catch (const std::exception&) {
        // Default dry-soil remains available — silently ignore missing/bad file.
    }
}

void SimEngine::solve() {
    if (mode == Mode::Fast)
        m_field = m_fast.solve(m_terrain, m_antennas, m_materials);
    else
        m_field = m_full.solve(m_terrain, m_antennas, m_materials);

    if (onFieldUpdated) onFieldUpdated();
}
