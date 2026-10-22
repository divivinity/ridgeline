#include "PointAnalysis.h"
#include "RFModels.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>

namespace {

constexpr float kEffectiveEarthRadiusM = 6371000.0f * 4.0f / 3.0f;

float valueAt(const std::vector<float>& values, std::size_t index) {
    return index < values.size()
        ? values[index] : -std::numeric_limits<float>::infinity();
}

std::string formatDeficit(const char* label, float actual, float required) {
    const float deficit = required - actual;
    char text[160]{};
    std::snprintf(text, sizeof(text), "%s is %.1f dB below the %.1f dB requirement.",
                  label, deficit, required);
    return text;
}

// Why a requested ITM prediction was replaced, or what the quick estimate
// leaves out. Empty when there is nothing the operator needs to know.
std::string pathModelCaveat(PathModel model, TerrainModel::LOSStatus los) {
    switch (model) {
        case PathModel::QuickEstimate:
            return los == TerrainModel::LOSStatus::Clear ? std::string{}
                : "Quick preview charges a flat 20 dB where terrain blocks the path; "
                  "Balanced and High accuracy model the diffraction.";
        case PathModel::AnalyticShortPath:
            return "ITM does not model paths under 1 km, so this one uses free-space "
                   "loss with terrain diffraction.";
        case PathModel::AnalyticOutOfBand:
            return "ITM covers 20 MHz to 20 GHz, so this frequency uses free-space "
                   "loss with terrain diffraction.";
        case PathModel::AnalyticItmRejected:
            return "ITM rejected this path's inputs, so it uses free-space loss with "
                   "terrain diffraction.";
        case PathModel::AnalyticItmMissing:
            return "This build has no ITM, so the path uses free-space loss with "
                   "terrain diffraction.";
        case PathModel::None:
        case PathModel::TerrainAnalytic:
        case PathModel::Itm:
            break;
    }
    return {};
}

} // namespace

PointAnalysisResult analyzeFieldPoint(const FieldGrid& grid,
                                      const TerrainModel& terrain,
                                      const std::vector<Antenna>& antennas,
                                      const ReceiverSettings& receiver,
                                      float worldX,
                                      float worldZ) {
    PointAnalysisResult result;
    if (!grid.valid() || grid.width <= 0 || grid.height <= 0 ||
        grid.cellSize <= 0.0f) {
        return result;
    }

    const glm::vec3 world = terrain.worldSize();
    worldX = std::clamp(worldX, 0.0f, world.x);
    worldZ = std::clamp(worldZ, 0.0f, world.z);
    result.gridX = std::clamp(static_cast<int>(std::lround(
        (worldX - grid.originXZ.x) / grid.cellSize)), 0, grid.width - 1);
    result.gridZ = std::clamp(static_cast<int>(std::lround(
        (worldZ - grid.originXZ.y) / grid.cellSize)), 0, grid.height - 1);
    const std::size_t index = static_cast<std::size_t>(result.gridZ) *
                            static_cast<std::size_t>(grid.width) + result.gridX;
    if (index >= grid.dbm.size()) return result;

    const glm::vec2 cellCenter = grid.cellCenter(result.gridX, result.gridZ);
    const float sampledWorldX = std::clamp(cellCenter.x, 0.0f, world.x);
    const float sampledWorldZ = std::clamp(cellCenter.y, 0.0f, world.z);
    result.valid = true;
    result.receiverPosition = {
        sampledWorldX,
        terrain.heightAtWorld(sampledWorldX, sampledWorldZ)
            + receiver.receiverHeightAglM,
        sampledWorldZ};
    result.desiredDbm = valueAt(grid.dbm, index);
    result.strongestSourceDbm = valueAt(grid.strongestDesiredDbm, index);
    result.jammerDbm = valueAt(grid.jammerDbm, index);
    result.snrDb = valueAt(grid.snrDb, index);
    result.sinrDb = valueAt(grid.sinrDb, index);
    result.jOverSDb = valueAt(grid.jOverSDb, index);
    result.fadeMarginDb = valueAt(grid.fadeMarginDb, index);
    result.noiseFloorDbm = grid.noiseFloorDbm;
    result.receiverSensitivityDbm = receiver.sensitivityDbm;
    result.requiredSnrDb = receiver.requiredSnrDb;
    result.fadeMarginTargetDb = receiver.fadeMarginTargetDb;
    result.inService = index < grid.service.size() && grid.service[index] != 0;
    if (index < grid.servingSourceIndex.size())
        result.servingSourceIndex = grid.servingSourceIndex[index];
    if (index < grid.strongestJammerIndex.size())
        result.strongestJammerIndex = grid.strongestJammerIndex[index];
    if (index < grid.servingPathModel.size() &&
        grid.servingPathModel[index] <= static_cast<std::uint8_t>(PathModel::AnalyticItmMissing))
        result.pathModel = static_cast<PathModel>(grid.servingPathModel[index]);

    if (!std::isfinite(result.desiredDbm) || result.servingSourceIndex < 0 ||
        result.servingSourceIndex >= static_cast<int>(antennas.size())) {
        result.explanations.emplace_back("No emitter reaches this result cell.");
        if (std::isfinite(result.jammerDbm) && result.strongestJammerIndex >= 0) {
            result.explanations.emplace_back(
                "An in-band jammer is present even though no desired signal is available.");
        }
        return result;
    }

    const Antenna& source = antennas[result.servingSourceIndex];
    result.distanceM = glm::distance(source.position, result.receiverPosition);
    result.losStatus = terrain.lineOfSightStatus(
        source.position, result.receiverPosition);
    result.peakObstacle = terrain.peakObstacle(
        source.position, result.receiverPosition);
    result.fresnelClearanceRatio = terrain.fresnelClearanceRatio(
        source.position, result.receiverPosition, source.frequencyMHz);
    // The same depth and loss the solvers charge for this path, so the
    // explanation can never disagree with the number on the map.
    if (terrain.hasVegetation()) {
        result.foliageDepthM = terrain.foliageDepthM(
            source.position, result.receiverPosition);
        result.foliageLossDb = RFModels::vegetationLossDb(
            result.foliageDepthM, source.frequencyMHz);
    }

    if (result.inService) {
        result.explanations.emplace_back(
            "Passes sensitivity, SINR, and fade-margin requirements.");
    } else {
        if (result.desiredDbm < receiver.sensitivityDbm) {
            result.explanations.push_back(formatDeficit(
                "Signal", result.desiredDbm, receiver.sensitivityDbm));
        }
        if (result.sinrDb < receiver.requiredSnrDb) {
            result.explanations.push_back(formatDeficit(
                "SINR", result.sinrDb, receiver.requiredSnrDb));
        }
        if (result.fadeMarginDb < receiver.fadeMarginTargetDb) {
            result.explanations.push_back(formatDeficit(
                "Fade margin", result.fadeMarginDb, receiver.fadeMarginTargetDb));
        }
    }
    if (result.losStatus == TerrainModel::LOSStatus::Blocked) {
        result.explanations.emplace_back(
            "Terrain blocks an extended part of the direct path.");
    } else if (result.losStatus == TerrainModel::LOSStatus::Diffracted) {
        result.explanations.emplace_back(
            "A narrow terrain obstruction requires diffraction.");
    } else if (result.fresnelClearanceRatio < 0.6f) {
        result.explanations.emplace_back(
            "The direct path is visible, but less than 60% of the first Fresnel zone is clear.");
    }
    if (const std::string caveat = pathModelCaveat(result.pathModel, result.losStatus);
        !caveat.empty())
        result.explanations.push_back(caveat);
    if (result.foliageLossDb >= 0.1f) {
        char text[160]{};
        std::snprintf(text, sizeof(text),
                      "The direct path crosses %.0f m of vegetation, costing %.1f dB.",
                      result.foliageDepthM, result.foliageLossDb);
        result.explanations.emplace_back(text);
    }
    if (std::isfinite(result.jammerDbm) && result.strongestJammerIndex >= 0) {
        result.explanations.emplace_back(
            "An in-band jammer contributes to the interference level.");
    }
    if (std::isfinite(result.strongestSourceDbm) &&
        result.desiredDbm - result.strongestSourceDbm > 0.25f) {
        result.explanations.emplace_back(
            "Multiple co-channel emitters contribute to the desired-signal total.");
    }

    const int pathSteps = terrain.adaptivePathSteps(
        source.position, result.receiverPosition, 160, 2048);
    const int samples = pathSteps + 1;
    result.profileDistanceM.reserve(samples);
    result.terrainElevationM.reserve(samples);
    result.lineOfSightElevationM.reserve(samples);
    result.fresnel60LowerElevationM.reserve(samples);
    if (terrain.hasVegetation()) result.canopyTopElevationM.reserve(samples);
    const glm::vec3 delta = result.receiverPosition - source.position;
    const float pathM = std::max(result.distanceM, 1.0f);
    const float horizontalDistanceM = glm::distance(
        glm::vec2(source.position.x, source.position.z),
        glm::vec2(result.receiverPosition.x, result.receiverPosition.z));
    const float lambdaM = 3.0e8f /
        std::max(source.frequencyMHz * 1.0e6f, 1.0f);
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(pathSteps);
        const glm::vec3 point = source.position + delta * t;
        const float d1 = pathM * t;
        const float d2 = pathM * (1.0f - t);
        const float radiusM = std::sqrt(std::max(0.0f, lambdaM * d1 * d2 / pathM));
        const float earthBulgeM = horizontalDistanceM * horizontalDistanceM *
            t * (1.0f - t) / (2.0f * kEffectiveEarthRadiusM);
        result.profileDistanceM.push_back(pathM * t);
        result.terrainElevationM.push_back(
            terrain.heightAtWorld(point.x, point.z) + earthBulgeM);
        result.lineOfSightElevationM.push_back(point.y);
        result.fresnel60LowerElevationM.push_back(point.y - 0.6f * radiusM);
        if (terrain.hasVegetation())
            result.canopyTopElevationM.push_back(
                terrain.canopyTopAtWorld(point.x, point.z) + earthBulgeM);
    }
    return result;
}
