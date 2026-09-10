#pragma once
#include "data/ScenarioIO.h"
#include <QString>
#include <algorithm>
#include <cmath>

namespace TerrainPresentation {
// Presentation only: never change terrain geometry or imply source accuracy.
inline QString areaSummary(double eastWestM, double northSouthM) {
    if (!std::isfinite(eastWestM) || !std::isfinite(northSouthM) ||
        eastWestM <= 0 || northSouthM <= 0)
        return QStringLiteral("Area unavailable");
    const bool metres = eastWestM < 1000 || northSouthM < 1000;
    const auto dimension = [metres](double value) {
        if (metres && value < 0.1) return QStringLiteral("<0.1");
        return QString::number(metres ? value : value / 1000, 'f', 1);
    };
    return QString("%1\u00a0×\u00a0%2\u00a0%3")
        .arg(dimension(eastWestM), dimension(northSouthM), metres ? "m" : "km");
}

inline QString gridDetails(const TerrainModel& terrain) {
    QString details = QString("Terrain grid: %1 × %2 samples\n"
                              "Sample spacing: %3 m east–west × %4 m north–south")
        .arg(terrain.width).arg(terrain.height)
        .arg(terrain.metersPerCellX(), 0, 'f', 1)
        .arg(terrain.metersPerCellZ(), 0, 'f', 1);
    if (terrain.sourceResolutionM > 0.0f) {
        // Resampling to a finer grid does not create detail, and a grid coarser
        // than its source throws detail away. Saying which is which is the only
        // honest way to report resolution.
        const float finestGrid = std::min(terrain.metersPerCellX(), terrain.metersPerCellZ());
        const char* relation = finestGrid < terrain.sourceResolutionM * 0.95f
            ? "\nThe grid is finer than the source, so the extra samples are interpolated."
            : (finestGrid > terrain.sourceResolutionM * 1.05f
                   ? "\nThe grid is coarser than the source, so some detail is not shown."
                   : "");
        details += QString("\nSource resolution: about %1 m%2")
                       .arg(terrain.sourceResolutionM, 0, 'f', 1)
                       .arg(relation);
    } else {
        details += "\nSource resolution is not known for this terrain.";
    }
    return details;
}

// Horizontal georeferencing and file format do not establish a vertical datum.
// Keep heights in the terrain's native reference until datum metadata is tracked.
inline QString elevationReference(TerrainSource::Type type) {
    if (type == TerrainSource::Type::Hills || type == TerrainSource::Type::Flat)
        return QStringLiteral("Synthetic heights · Demo only");
    return QStringLiteral("Terrain heights · Vertical datum unverified");
}

inline QString elevationCredit(TerrainSource::Type type,
        TerrainModel::ElevationProvider provider = TerrainModel::ElevationProvider::Unspecified) {
    switch (type) {
    case TerrainSource::Type::Global:
        if (provider == TerrainModel::ElevationProvider::Mapzen)
            return QStringLiteral("Elevation © Mapzen and source contributors");
        if (provider == TerrainModel::ElevationProvider::Custom)
            return QStringLiteral("Elevation: custom Terrarium source · Attribution unverified");
        return QStringLiteral("Elevation: streamed terrain · Provider unverified");
    case TerrainSource::Type::Hgt:
        return QStringLiteral("Elevation: imported HGT");
    case TerrainSource::Type::Dted:
        return QStringLiteral("Elevation: imported DTED");
    case TerrainSource::Type::GeoTiff:
        return QStringLiteral("Elevation: imported GeoTIFF");
    default:
        return QStringLiteral("Synthetic elevation · Demo only");
    }
}
}
