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
        Fast = Preview,
        Full = Engineering
    };

    Mode mode = Mode::Planner;
    ReceiverSettings receiver;

    void setTerrain(TerrainModel terrain);
    void setAntennas(std::vector<Antenna> antennas);
    void addAntenna(Antenna antenna);
    void removeAntenna(int index);
    void updateAntenna(int index, Antenna antenna);
    void setFieldGrid(FieldGrid field);

    void loadMaterials(const std::string& path);

    void solve(const std::function<void(int, int)>& progress = {},
               const std::function<bool()>& cancelled = {});

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
