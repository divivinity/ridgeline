#include "SimEngine.h"
#include "RFModels.h"
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

// heightAtWorld() samples a 2x2 neighbourhood, so it needs a populated field.
bool hasSamples(const TerrainModel& terrain) {
    return terrain.width > 1 && terrain.height > 1 && !terrain.heights.empty();
}

float groundAt(const TerrainModel& terrain, float worldX, float worldZ) {
    return hasSamples(terrain) ? terrain.heightAtWorld(worldX, worldZ) : 0.0f;
}

// Where a source sat as a fraction of the map, 0..1. A degenerate extent has
// no meaningful fraction, so treat it as the middle.
float extentFraction(float position, float extent) {
    if (!(extent > 0.0f)) return 0.5f;
    return std::clamp(position / extent, 0.0f, 1.0f);
}

// A source with no geographic position holds only procedural world metres,
// which mean nothing on a different map: carried over verbatim they land
// wherever those metres happen to fall, typically bunched into a corner.
// Remember where such a source sat proportionally, and how far above ground,
// so it can be placed equivalently on the new terrain. Sources that do have a
// geographic position are left entirely alone — their real-world location is
// authoritative and is preserved exactly, even off the edge of the new map.
struct ProceduralPlacement {
    bool  carry = false;
    float fracX = 0.5f;
    float fracZ = 0.5f;
    float aglM  = 0.0f;
};

ProceduralPlacement captureProcedural(const Antenna& antenna,
                                      const TerrainModel& terrain) {
    if (antenna.hasGeoPosition) return {};
    const glm::vec3 size = terrain.worldSize();
    ProceduralPlacement placement;
    placement.carry = true;
    placement.fracX = extentFraction(antenna.position.x, size.x);
    placement.fracZ = extentFraction(antenna.position.z, size.z);
    placement.aglM  = std::max(
        0.0f,
        antenna.position.y - groundAt(terrain, antenna.position.x, antenna.position.z));
    return placement;
}

void applyProcedural(Antenna& antenna, const ProceduralPlacement& placement,
                     const TerrainModel& terrain) {
    if (!placement.carry) return;
    const glm::vec3 size = terrain.worldSize();
    antenna.position.x = placement.fracX * size.x;
    antenna.position.z = placement.fracZ * size.z;
    antenna.position.y =
        groundAt(terrain, antenna.position.x, antenna.position.z) + placement.aglM;
}

} // namespace

void SimEngine::setTerrain(TerrainModel terrain) {
    terrain.validate();
    if (m_terrain.hasGeoReference()) {
        for (auto& antenna : m_antennas)
            syncGeoFromWorld(antenna, m_terrain);
    }

    // Capture relative placement for any source still lacking a geographic
    // position, while the outgoing terrain's extent is still available.
    std::vector<ProceduralPlacement> carried;
    carried.reserve(m_antennas.size());
    for (const auto& antenna : m_antennas)
        carried.push_back(captureProcedural(antenna, m_terrain));

    m_terrain = std::move(terrain);

    for (std::size_t i = 0; i < m_antennas.size(); ++i)
        applyProcedural(m_antennas[i], carried[i], m_terrain);

    if (m_terrain.hasGeoReference()) {
        for (auto& antenna : m_antennas)
            projectGeoToWorld(antenna, m_terrain);
        // Sources just placed proportionally now sit on a georeferenced map;
        // give them a real position so every later switch takes the exact
        // geographic path above. Sources that already had one are untouched.
        for (std::size_t i = 0; i < m_antennas.size(); ++i)
            if (carried[i].carry)
                syncGeoFromWorld(m_antennas[i], m_terrain);
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

    FastSolver fast = m_fast;
    FullSolver full = m_full;
    fast.receiver = receiver;
    full.receiver = receiver;
    FieldGrid result;

    if (mode == Mode::Preview) {
        fast.gridResolution = adaptiveResolution(30.0f, 240);
        result = fast.solve(m_terrain, m_antennas, m_materials, progress, cancelled);
    } else if (mode == Mode::Planner) {
        full.gridResolution = adaptiveResolution(20.0f, 360);
        full.enableGroundReflection = false;
        full.enableRain = false;
        full.enableFresnelPenalty = true;
        full.enableMultipleDiffraction = true;
        full.enableItm = false;
        full.requiredFresnelClearance = 0.6f;
        result = full.solve(m_terrain, m_antennas, m_materials, progress, cancelled);
    } else {
        full.gridResolution = adaptiveResolution(10.0f, 600);
        full.enableGroundReflection = true;
        full.enableRain = true;
        full.enableFresnelPenalty = true;
        full.enableMultipleDiffraction = true;
        full.enableItm = true;
        full.requiredFresnelClearance = 0.6f;
        result = full.solve(m_terrain, m_antennas, m_materials, progress, cancelled);
    }

    if (calibration.enabled &&
        std::isfinite(calibration.desiredPowerOffsetDb) &&
        calibration.desiredPowerOffsetDb != 0.0f) {
        for (float& value : result.dbm) {
            if (std::isfinite(value))
                value += calibration.desiredPowerOffsetDb;
        }
        for (float& value : result.strongestDesiredDbm) {
            if (std::isfinite(value))
                value += calibration.desiredPowerOffsetDb;
        }
        // Interference is deliberately left untouched. Recompute every
        // receiver-derived metric and service decision from the corrected
        // desired signal field.
        RFModels::finalizeMetrics(result, receiver);
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
