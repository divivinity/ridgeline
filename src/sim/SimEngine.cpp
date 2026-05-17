#include "SimEngine.h"

void SimEngine::setTerrain(TerrainModel terrain) {
    m_terrain = std::move(terrain);
}

void SimEngine::setAntennas(std::vector<Antenna> antennas) {
    m_antennas = std::move(antennas);
}

void SimEngine::solve() {
    if (mode == Mode::Fast) {
        m_field = m_fast.solve(m_terrain, m_antennas, m_materials);
    }
    // Mode::Full added in Task 5
    if (onFieldUpdated) onFieldUpdated();
}
