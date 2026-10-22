#pragma once

#include "TerrainModel.h"
#include "Types.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

struct PointAnalysisResult {
    bool valid = false;
    bool inService = false;
    glm::vec3 receiverPosition{0.0f};
    int gridX = -1;
    int gridZ = -1;
    int servingSourceIndex = -1;
    int strongestJammerIndex = -1;
    PathModel pathModel = PathModel::None;

    float desiredDbm = 0.0f;
    float strongestSourceDbm = 0.0f;
    float jammerDbm = 0.0f;
    float noiseFloorDbm = 0.0f;
    float snrDb = 0.0f;
    float sinrDb = 0.0f;
    float jOverSDb = 0.0f;
    float fadeMarginDb = 0.0f;
    float distanceM = 0.0f;
    float fresnelClearanceRatio = 0.0f;
    // Metres of the serving path inside canopy, and the excess loss the solver
    // charges for them. Both stay zero when the terrain carries no vegetation.
    float foliageDepthM = 0.0f;
    float foliageLossDb = 0.0f;
    TerrainModel::LOSStatus losStatus = TerrainModel::LOSStatus::Blocked;
    TerrainModel::PeakObstacle peakObstacle{0.0f, 0.5f};

    float receiverSensitivityDbm = -100.0f;
    float requiredSnrDb = 10.0f;
    float fadeMarginTargetDb = 10.0f;

    std::vector<float> profileDistanceM;
    std::vector<float> terrainElevationM;
    std::vector<float> lineOfSightElevationM;
    std::vector<float> fresnel60LowerElevationM;
    // Canopy top along the profile, parallel to terrainElevationM. Empty when
    // the terrain carries no vegetation.
    std::vector<float> canopyTopElevationM;
    std::vector<std::string> explanations;
};

// A short name for the model that computed a serving path, for display.
inline std::string pathModelName(PathModel model) {
    switch (model) {
        case PathModel::None: return "No serving path";
        case PathModel::QuickEstimate: return "Quick preview estimate";
        case PathModel::TerrainAnalytic: return "Free space + terrain diffraction";
        case PathModel::Itm: return "NTIA ITM";
        case PathModel::AnalyticShortPath:
        case PathModel::AnalyticOutOfBand:
        case PathModel::AnalyticItmRejected:
        case PathModel::AnalyticItmMissing:
            return "Free space + terrain diffraction (ITM not used)";
    }
    return "Unknown model";
}

PointAnalysisResult analyzeFieldPoint(const FieldGrid& grid,
                                      const TerrainModel& terrain,
                                      const std::vector<Antenna>& antennas,
                                      const ReceiverSettings& receiver,
                                      float worldX,
                                      float worldZ);
