#include "SimEngine.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace {

void syncGeoFromWorld(Antenna& antenna, const TerrainModel& terrain) {
    if (!terrain.hasGeoReference()) return;

    const auto ll = terrain.worldToLatLon(antenna.position.x, antenna.position.z);
    antenna.hasGeoPosition = true;
    antenna.latitudeDeg = ll.latDeg;
    antenna.longitudeDeg = ll.lonDeg;
    antenna.heightAglM = std::max(
        0.0f,
        antenna.position.y - terrain.heightAtWorld(antenna.position.x, antenna.position.z));
}

void projectGeoToWorld(Antenna& antenna, const TerrainModel& terrain) {
    if (!terrain.hasGeoReference() || !antenna.hasGeoPosition) return;

    const glm::vec2 world = terrain.latLonToWorld(
        antenna.latitudeDeg, antenna.longitudeDeg);
    antenna.position.x = world.x;
    antenna.position.z = world.y;
    antenna.position.y = terrain.heightAtWorld(world.x, world.y)
                       + std::max(0.0f, antenna.heightAglM);
}

} // namespace

void SimEngine::setTerrain(TerrainModel terrain) {
    // Capture authoritative WGS84 positions from the old terrain before it is
    // replaced. This preserves sources across changes in DEM resolution or
    // analysis extent instead of leaving stale local X/Z coordinates behind.
    if (m_terrain.hasGeoReference()) {
        for (auto& antenna : m_antennas)
            syncGeoFromWorld(antenna, m_terrain);
    }

    m_terrain = std::move(terrain);

    // Reproject any geospatial sources into the new terrain coordinate system.
    if (m_terrain.hasGeoReference()) {
        for (auto& antenna : m_antennas)
            projectGeoToWorld(antenna, m_terrain);
    }
}

void SimEngine::setAntennas(std::vector<Antenna> antennas) {
    if (m_terrain.hasGeoReference()) {
        for (auto& antenna : antennas) {
            if (antenna.hasGeoPosition)
                projectGeoToWorld(antenna, m_terrain);
            else
                syncGeoFromWorld(antenna, m_terrain);
        }
    }
    m_antennas = std::move(antennas);
}

void SimEngine::setFieldGrid(FieldGrid field) {
    m_field = std::move(field);
    if (onFieldUpdated) onFieldUpdated();
}

void SimEngine::loadMaterials(const std::string& path) {
    try {
        m_materials.load(path);
    } catch (const std::exception&) {
        // Default dry-soil remains available — silently ignore missing/bad file.
    }
}

void SimEngine::solve(const std::function<void(int, int)>& progress,
                      const std::function<bool()>& cancelled) {
    if (cancelled && cancelled())
        throw std::runtime_error("Coverage solve cancelled");

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
        result = fast.solve(m_terrain, m_antennas, m_materials, progress, cancelled);
    } else if (mode == Mode::Planner) {
        full.gridResolution = adaptiveResolution(20.0f, 360);
        full.enableGroundReflection = false;
        full.enableRain = false;
        full.enableFresnelPenalty = true;
        full.requiredFresnelClearance = 0.6f;
        result = full.solve(m_terrain, m_antennas, m_materials, progress, cancelled);
    } else {
        full.gridResolution = adaptiveResolution(10.0f, 600);
        full.enableGroundReflection = true;
        full.enableRain = true;
        full.enableFresnelPenalty = true;
        full.requiredFresnelClearance = 0.6f;
        result = full.solve(m_terrain, m_antennas, m_materials, progress, cancelled);
    }

    if (cancelled && cancelled())
        throw std::runtime_error("Coverage solve cancelled");
    setFieldGrid(std::move(result));
}

void SimEngine::addAntenna(Antenna antenna) {
    if (m_terrain.hasGeoReference()) {
        if (antenna.hasGeoPosition)
            projectGeoToWorld(antenna, m_terrain);
        else
            syncGeoFromWorld(antenna, m_terrain);
    }
    m_antennas.push_back(std::move(antenna));
}

void SimEngine::removeAntenna(int index) {
    if (index < 0 || index >= static_cast<int>(m_antennas.size())) return;
    m_antennas.erase(m_antennas.begin() + index);
}

void SimEngine::updateAntenna(int index, Antenna antenna) {
    if (index < 0 || index >= static_cast<int>(m_antennas.size())) return;
    if (m_terrain.hasGeoReference())
        syncGeoFromWorld(antenna, m_terrain);
    m_antennas[index] = std::move(antenna);
}
