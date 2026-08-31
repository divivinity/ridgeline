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
    enum class Mode {
        Preview,
        Planner,
        Engineering,
        Fast = Preview,       // backwards-compatible scenario/test alias
        Full = Engineering    // backwards-compatible scenario/test alias
    };

    Mode mode = Mode::Planner;

    void setTerrain(TerrainModel terrain);
    void setAntennas(std::vector<Antenna> antennas);
    void addAntenna(Antenna antenna);
    void removeAntenna(int index);   // no-op if index out of range
    void updateAntenna(int index, Antenna antenna);  // no-op if index out of range

    // Load EM material properties from JSON.
    // Silent no-op on failure (default dry-soil always available).
    void loadMaterials(const std::string& path);

    // Progress reports completed and total terrain rows. The callback may run
    // on a worker thread when the UI launches an asynchronous solve.
    void solve(const std::function<void(int, int)>& progress = {});

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
