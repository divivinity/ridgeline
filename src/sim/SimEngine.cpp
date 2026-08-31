#include "SimEngine.h"
#include <stdexcept>
#include <algorithm>

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

void SimEngine::solve(const std::function<void(int, int)>& progress) {
    const glm::vec3 world = m_terrain.worldSize();
    const float longestAxis = std::max(world.x, world.z);
    auto adaptiveResolution = [longestAxis](float preferredM, int maxCellsPerAxis) {
        return std::max(preferredM, longestAxis / static_cast<float>(maxCellsPerAxis));
    };
    if (mode == Mode::Preview) {
        m_fast.gridResolution = adaptiveResolution(30.0f, 240);
        m_field = m_fast.solve(m_terrain, m_antennas, m_materials, progress);
    } else if (mode == Mode::Planner) {
        m_full.gridResolution = adaptiveResolution(20.0f, 360);
        m_full.enableGroundReflection = false;
        m_full.enableRain = false;
        m_full.enableFresnelPenalty = true;
        m_full.requiredFresnelClearance = 0.6f;
        m_field = m_full.solve(m_terrain, m_antennas, m_materials, progress);
    } else {
        m_full.gridResolution = adaptiveResolution(10.0f, 600);
        m_full.enableGroundReflection = true;
        m_full.enableRain = true;
        m_full.enableFresnelPenalty = true;
        m_full.requiredFresnelClearance = 0.6f;
        m_field = m_full.solve(m_terrain, m_antennas, m_materials, progress);
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
