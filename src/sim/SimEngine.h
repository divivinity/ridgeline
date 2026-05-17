#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "FastSolver.h"
#include "FullSolver.h"
#include "MaterialLibrary.h"
#include <vector>
#include <functional>
#include <string>

class SimEngine {
public:
    enum class Mode { Fast, Full };

    Mode mode = Mode::Fast;

    void setTerrain(TerrainModel terrain);
    void setAntennas(std::vector<Antenna> antennas);

    // Load EM material properties from JSON.
    // Silent no-op on failure (default dry-soil always available).
    void loadMaterials(const std::string& path);

    void solve();  // dispatches to FastSolver or FullSolver; fires onFieldUpdated

    const FieldGrid&            fieldGrid() const { return m_field; }
    const TerrainModel&         terrain()   const { return m_terrain; }
    const std::vector<Antenna>& antennas()  const { return m_antennas; }

    std::function<void()> onFieldUpdated;

private:
    TerrainModel         m_terrain;
    std::vector<Antenna> m_antennas;
    FieldGrid            m_field;
    FastSolver           m_fast;
    FullSolver           m_full;
    MaterialLibrary      m_materials;
};
