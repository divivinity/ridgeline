#include "map/MapMeasure.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371008.8;   // IUGG mean radius

double toRadians(double degrees) { return degrees * kPi / 180.0; }
}

double MapMeasure::haversineMetres(double lat1Deg, double lon1Deg,
                                   double lat2Deg, double lon2Deg) {
    if (!std::isfinite(lat1Deg) || !std::isfinite(lon1Deg) ||
        !std::isfinite(lat2Deg) || !std::isfinite(lon2Deg))
        return std::numeric_limits<double>::quiet_NaN();

    const double lat1 = toRadians(lat1Deg);
    const double lat2 = toRadians(lat2Deg);
    const double dLat = lat2 - lat1;
    // Longitude difference is wrapped so a path across the antimeridian is the
    // short way round rather than most of the way about the earth.
    double dLon = std::fmod(lon2Deg - lon1Deg + 540.0, 360.0) - 180.0;
    dLon = toRadians(dLon);

    const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
                     std::cos(lat1) * std::cos(lat2) *
                     std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(a)));
}

double MapMeasure::initialBearingDeg(double lat1Deg, double lon1Deg,
                                     double lat2Deg, double lon2Deg) {
    if (!std::isfinite(lat1Deg) || !std::isfinite(lon1Deg) ||
        !std::isfinite(lat2Deg) || !std::isfinite(lon2Deg))
        return std::numeric_limits<double>::quiet_NaN();

    const double lat1 = toRadians(lat1Deg);
    const double lat2 = toRadians(lat2Deg);
    const double dLon = toRadians(std::fmod(lon2Deg - lon1Deg + 540.0, 360.0) - 180.0);

    const double y = std::sin(dLon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) -
                     std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
    const double bearing = std::atan2(y, x) * 180.0 / kPi;
    return std::fmod(bearing + 360.0, 360.0);
}

double MapMeasure::planarMetres(double x1, double z1, double x2, double z2) {
    return std::hypot(x2 - x1, z2 - z1);
}

MapMeasure::ScaleBar MapMeasure::chooseScaleBar(double metresPerPixel, double maxPixels) {
    ScaleBar bar;
    if (!std::isfinite(metresPerPixel) || metresPerPixel <= 0.0 ||
        !std::isfinite(maxPixels) || maxPixels <= 0.0)
        return bar;

    const double maxMetres = metresPerPixel * maxPixels;
    if (!(maxMetres > 0.0)) return bar;

    // Walk down from the largest power of ten that fits, trying 5, 2 then 1, so
    // the bar always reads as a round number rather than an arbitrary width.
    const double magnitude = std::pow(10.0, std::floor(std::log10(maxMetres)));
    for (const double step : {5.0, 2.0, 1.0}) {
        const double candidate = step * magnitude;
        if (candidate <= maxMetres) {
            bar.metres = candidate;
            bar.pixels = candidate / metresPerPixel;
            bar.label = formatDistance(candidate);
            return bar;
        }
    }
    // maxMetres sits below the magnitude only through rounding; fall back to a
    // tenth of it, which is always representable.
    bar.metres = magnitude / 10.0;
    bar.pixels = bar.metres / metresPerPixel;
    bar.label = formatDistance(bar.metres);
    return bar;
}

QString MapMeasure::formatDistance(double metres) {
    if (!std::isfinite(metres) || metres < 0.0) return QStringLiteral("unavailable");
    if (metres < 1.0) return QString::number(metres, 'f', 2) + " m";
    if (metres < 1000.0) {
        const double rounded = std::round(metres * 10.0) / 10.0;
        return QString::number(rounded, 'f', rounded < 10.0 ? 1 : 0) + " m";
    }
    const double km = metres / 1000.0;
    return QString::number(km, 'f', km < 10.0 ? 2 : 1) + " km";
}

QString MapMeasure::compassPoint(double bearingDeg) {
    if (!std::isfinite(bearingDeg)) return {};
    static const char* points[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                   "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    const double wrapped = std::fmod(std::fmod(bearingDeg, 360.0) + 360.0, 360.0);
    const int index = static_cast<int>(std::lround(wrapped / 22.5)) % 16;
    return QString::fromLatin1(points[index]);
}
