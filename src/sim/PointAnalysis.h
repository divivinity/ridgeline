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
    TerrainModel::LOSStatus losStatus = TerrainModel::LOSStatus::Blocked;
    TerrainModel::PeakObstacle peakObstacle{0.0f, 0.5f};

    float receiverSensitivityDbm = -100.0f;
    float requiredSnrDb = 10.0f;
    float fadeMarginTargetDb = 10.0f;

    std::vector<float> profileDistanceM;
    std::vector<float> terrainElevationM;
    std::vector<float> lineOfSightElevationM;
    std::vector<float> fresnel60LowerElevationM;
    std::vector<std::string> explanations;
};

PointAnalysisResult analyzeFieldPoint(const FieldGrid& grid,
                                      const TerrainModel& terrain,
                                      const std::vector<Antenna>& antennas,
                                      const ReceiverSettings& receiver,
                                      float worldX,
                                      float worldZ);
