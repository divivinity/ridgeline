#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "VegetationAreas.h"
#include "FastSolver.h"
#include "FullSolver.h"
#include "MaterialLibrary.h"
#include <vector>
#include <functional>
#include <string>

struct CoverageCalibrationSettings {
    bool enabled = false;
    float desiredPowerOffsetDb = 0.0f;
};

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
    CoverageCalibrationSettings calibration;

    void setTerrain(TerrainModel terrain);

    // Hand-drawn vegetation. Setting either terrain or areas re-rasterises the
    // canopy grid, so the two can never drift apart.
    void setVegetation(std::vector<VegetationArea> areas);
    const std::vector<VegetationArea>& vegetation() const { return m_vegetation; }
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
    std::vector<VegetationArea> m_vegetation;
    std::vector<Antenna> m_antennas;
    FieldGrid            m_field;
    FastSolver           m_fast;
    FullSolver           m_full;
    MaterialLibrary      m_materials;
};
