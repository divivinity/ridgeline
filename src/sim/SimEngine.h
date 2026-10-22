#pragma once
#include "Types.h"
#include "TerrainModel.h"
#include "VegetationAreas.h"
#include "FastSolver.h"
#include "FullSolver.h"
#include "MaterialLibrary.h"
#include <cstdint>
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

    // Hand-drawn stands and clearings. Setting terrain or areas re-rasterises the
    // canopy grid, so the two can never drift apart - except that a list whose
    // footprints are unchanged (a rename) is stored without a rebuild, so
    // canopyRevision stays put and nothing downstream mistakes it for an edit.
    //
    // Areas are expressed in the world metres of the terrain currently
    // installed. setTerrain carries existing areas onto a new terrain itself.
    void setVegetation(std::vector<VegetationArea> areas);
    const std::vector<VegetationArea>& vegetation() const { return m_vegetation; }

    // An imported canopy layer, supplied as something that can place itself on
    // any terrain grid rather than as a grid.
    //
    // A callback, not a raster, so the simulation core keeps no dependency on
    // the data layer that fetches and decodes one - and, more importantly, so
    // the import survives a terrain reload. A resampled grid would be silently
    // wrong the moment the terrain resolution changed; a sampler is asked
    // again.
    //
    // Returning false means "this source does not describe this ground", and
    // the canopy falls back to whatever has been drawn by hand.
    using CanopySampler = std::function<bool(const TerrainModel&,
                                             std::vector<std::uint16_t>&,
                                             std::vector<std::uint8_t>&)>;
    void setImportedCanopy(CanopySampler sampler, CanopySourceInfo info);
    void clearImportedCanopy();
    const CanopySourceInfo& importedCanopy() const { return m_canopyInfo; }
    bool hasImportedCanopy() const { return static_cast<bool>(m_canopySampler); }
    // Resamples the imported layer onto the current terrain without touching the
    // canopy grid. False when there is no import or it declines this terrain.
    // Used to suggest a drawn stand's height from measured data.
    bool sampleImportedCanopy(std::vector<std::uint16_t>& heightDm,
                              std::vector<std::uint8_t>& coverPct) const;
    // Bumped whenever the canopy grid is rebuilt. A renderer caching anything
    // derived from vegetation compares this rather than the grid itself, which
    // at the 1024x1024 ceiling would be a megabyte to re-hash on every layer
    // toggle.
    std::uint64_t canopyRevision() const { return m_canopyRevision; }
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
    // Rebuilds the canopy grid from the imported layer and the drawn areas.
    void rebuildCanopy();

    TerrainModel         m_terrain;
    std::vector<VegetationArea> m_vegetation;
    CanopySampler        m_canopySampler;
    CanopySourceInfo     m_canopyInfo;
    std::uint64_t        m_canopyRevision = 0;
    std::vector<Antenna> m_antennas;
    FieldGrid            m_field;
    FastSolver           m_fast;
    FullSolver           m_full;
    MaterialLibrary      m_materials;
};
