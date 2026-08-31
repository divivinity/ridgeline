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

    // Configure solver snapshots locally. This keeps a background solve from
    // mutating the engine's persistent solver configuration while the UI owns
    // the SimEngine instance. Only the completed FieldGrid is committed back.
    FastSolver fast = m_fast;
    FullSolver full = m_full;
    FieldGrid result;

    if (mode == Mode::Preview) {
        fast.gridResolution = adaptiveResolution(30.0f, 240);
        result = fast.solve(m_terrain, m_antennas, m_materials, progress);
    } else if (mode == Mode::Planner) {
        full.gridResolution = adaptiveResolution(20.0f, 360);
        full.enableGroundReflection = false;
        full.enableRain = false;
        full.enableFresnelPenalty = true;
        full.requiredFresnelClearance = 0.6f;
        result = full.solve(m_terrain, m_antennas, m_materials, progress);
    } else {
        full.gridResolution = adaptiveResolution(10.0f, 600);
        full.enableGroundReflection = true;
        full.enableRain = true;
        full.enableFresnelPenalty = true;
        full.requiredFresnelClearance = 0.6f;
        result = full.solve(m_terrain, m_antennas, m_materials, progress);
    }

    m_field = std::move(result);
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
