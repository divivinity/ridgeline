#include "Exporter.h"
#include <QImage>
#include <QString>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

// ── colormap ─────────────────────────────────────────────────────────────────
// Maps t in [0,1] to RGB:  0 = weak (red), 0.5 = medium (yellow), 1 = strong (green)
static void colormapRgb(float t, uint8_t& r, uint8_t& g, uint8_t& b) {
    t = std::max(0.0f, std::min(1.0f, t));
    if (t < 0.5f) {
        // red → yellow
        const float s = t * 2.0f;
        r = 255;
        g = static_cast<uint8_t>(255.0f * s);
        b = 0;
    } else {
        // yellow → green
        const float s = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(255.0f * (1.0f - s));
        g = 255;
        b = 0;
    }
}

// ── PNG export ────────────────────────────────────────────────────────────────
bool Exporter::exportPng(const FieldGrid& grid, const std::string& path) {
    if (!grid.valid()) return false;

    // Find finite min/max for normalization.
    float minDbm =  1e30f;
    float maxDbm = -1e30f;
    for (float v : grid.dbm) {
        if (std::isfinite(v)) {
            minDbm = std::min(minDbm, v);
            maxDbm = std::max(maxDbm, v);
        }
    }
    // Guard: all NaN or single value
    const float range = (maxDbm > minDbm) ? (maxDbm - minDbm) : 1.0f;

    QImage img(grid.width, grid.height, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    for (int y = 0; y < grid.height; ++y) {
        auto* line = reinterpret_cast<uint32_t*>(img.scanLine(y));
        for (int x = 0; x < grid.width; ++x) {
            const float v = grid.at(x, y);
            if (!std::isfinite(v)) continue;
            uint8_t r, g, b;
            colormapRgb((v - minDbm) / range, r, g, b);
            line[x] = qRgba(r, g, b, 255);
        }
    }

    return img.save(QString::fromStdString(path));
}

// ── CSV export ────────────────────────────────────────────────────────────────
bool Exporter::exportCsv(const FieldGrid& grid, const std::string& path) {
    TerrainModel unreferenced;
    return exportCsv(grid, unreferenced, path);
}

bool Exporter::exportCsv(const FieldGrid& grid, const TerrainModel& terrain,
                         const std::string& path) {
    if (!grid.valid()) return false;

    std::ofstream f(path);
    if (!f) return false;

    if (terrain.hasGeoReference())
        f << "x_m,z_m,latitude,longitude,terrain_elevation_m,dbm\n";
    else
        f << "x_m,z_m,dbm\n";
    for (int y = 0; y < grid.height; ++y) {
        const float z_m = grid.originXZ.y + static_cast<float>(y) * grid.cellSize;
        for (int x = 0; x < grid.width; ++x) {
            const float x_m = grid.originXZ.x + static_cast<float>(x) * grid.cellSize;
            const float v   = grid.at(x, y);
            f << std::fixed << std::setprecision(3) << x_m << ',' << z_m << ',';
            if (terrain.hasGeoReference()) {
                const auto ll = terrain.worldToLatLon(x_m, z_m);
                f << std::setprecision(8) << ll.latDeg << ',' << ll.lonDeg << ','
                  << std::setprecision(2) << terrain.heightAtWorld(x_m, z_m) << ',';
            }
            if (std::isfinite(v)) f << v;
            else                  f << "nan";
            f << '\n';
        }
    }
    return f.good();
}
