#include "map/Hillshade.h"
#include "sim/TerrainModel.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

QImage Hillshade::render(const TerrainModel& terrain,
                         double azimuthDeg, double altitudeDeg) {
    if (terrain.width < 2 || terrain.height < 2) return {};
    if (static_cast<std::size_t>(terrain.width) * terrain.height != terrain.heights.size())
        return {};

    const double cellX = static_cast<double>(terrain.metersPerCellX());
    const double cellZ = static_cast<double>(terrain.metersPerCellZ());
    if (!std::isfinite(cellX) || !std::isfinite(cellZ) || cellX <= 0.0 || cellZ <= 0.0)
        return {};

    const double zenith = (90.0 - altitudeDeg) * kPi / 180.0;
    // Compass bearing (clockwise from north) to math angle (anticlockwise from east).
    const double azimuth = (360.0 - azimuthDeg + 90.0) * kPi / 180.0;

    const auto heightAt = [&terrain](int x, int z) {
        const int cx = std::clamp(x, 0, terrain.width - 1);
        const int cz = std::clamp(z, 0, terrain.height - 1);
        return static_cast<double>(terrain.heights[cz * terrain.width + cx]);
    };

    QImage image(terrain.width, terrain.height, QImage::Format_Grayscale8);
    if (image.isNull()) return {};

    for (int z = 0; z < terrain.height; ++z) {
        uchar* line = image.scanLine(z);
        for (int x = 0; x < terrain.width; ++x) {
            // Horn's method over the eight neighbours. Edges clamp, so the
            // border shades from a one-sided difference rather than a seam.
            const double a = heightAt(x - 1, z - 1);
            const double b = heightAt(x,     z - 1);
            const double c = heightAt(x + 1, z - 1);
            const double d = heightAt(x - 1, z);
            const double f = heightAt(x + 1, z);
            const double g = heightAt(x - 1, z + 1);
            const double h = heightAt(x,     z + 1);
            const double i = heightAt(x + 1, z + 1);

            const double dzdx = ((c + 2.0 * f + i) - (a + 2.0 * d + g)) / (8.0 * cellX);
            const double dzdz = ((g + 2.0 * h + i) - (a + 2.0 * b + c)) / (8.0 * cellZ);

            const double slope = std::atan(std::hypot(dzdx, dzdz));
            double aspect = std::atan2(dzdz, -dzdx);
            if (aspect < 0.0) aspect += 2.0 * kPi;

            double value = std::cos(zenith) * std::cos(slope) +
                           std::sin(zenith) * std::sin(slope) * std::cos(azimuth - aspect);
            value = std::clamp(value, 0.0, 1.0);
            line[x] = static_cast<uchar>(std::lround(value * 255.0));
        }
    }

    // Terrain rows run south to north; image rows run top to bottom.
    return image.mirrored(false, true);
}
