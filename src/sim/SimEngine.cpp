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
    if (mode == Mode::Preview) {
        m_fast.gridResolution = 30.0f;
        m_field = m_fast.solve(m_terrain, m_antennas, m_materials);
    } else if (mode == Mode::Planner) {
        m_full.gridResolution = 20.0f;
        m_full.enableGroundReflection = false;
        m_full.enableRain = false;
        m_full.enableFresnelPenalty = true;
        m_full.requiredFresnelClearance = 0.6f;
        m_field = m_full.solve(m_terrain, m_antennas, m_materials);
    } else {
        m_full.gridResolution = 10.0f;
        m_full.enableGroundReflection = true;
        m_full.enableRain = true;
        m_full.enableFresnelPenalty = true;
        m_full.requiredFresnelClearance = 0.6f;
        m_field = m_full.solve(m_terrain, m_antennas, m_materials);
    }

    if (onFieldUpdated) onFieldUpdated();
}

void SimEngine::addAntenna(Antenna antenna) {
    m_antennas.push_back(std::move(antenna));
}

void SimEngine::removeAntenna(int index) {
    if (index < 0 || index >= static_cast<int>(m_antennas.size())) return;
    m_antennas.erase(m_antennas.begin() + index);
}

void SimEngine::updateAntenna(int index, Antenna antenna) {
    if (index < 0 || index >= static_cast<int>(m_antennas.size())) return;
    m_antennas[index] = std::move(antenna);
}
