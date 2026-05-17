#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "FastSolver.h"
#include <vector>
#include <functional>

class SimEngine {
public:
    enum class Mode { Fast, Full };

    Mode mode = Mode::Fast;

    void setTerrain(TerrainModel terrain);
    void setAntennas(std::vector<Antenna> antennas);
    void solve();  // runs solver for current mode, fires onFieldUpdated

    const FieldGrid& fieldGrid() const { return m_field; }
    const TerrainModel& terrain() const { return m_terrain; }

    std::function<void()> onFieldUpdated;  // fires after each solve

private:
    TerrainModel         m_terrain;
    std::vector<Antenna> m_antennas;
    FieldGrid            m_field;
    FastSolver           m_fast;
};
