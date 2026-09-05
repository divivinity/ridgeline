#include "Mgrs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kWgs84SemiMajorM = 6378137.0;
constexpr double kWgs84EccentricitySq = 0.0066943799901413165;
constexpr double kUtmScale = 0.9996;

constexpr const char* kLatitudeBands = "CDEFGHJKLMNPQRSTUVWX";
constexpr std::array<const char*, 3> kColumnSets{
    "ABCDEFGH", "JKLMNPQR", "STUVWXYZ"
};
constexpr std::array<const char*, 2> kRowSets{
    "ABCDEFGHJKLMNPQRSTUV", "FGHJKLMNPQRSTUVABCDE"
};
constexpr std::array<double, 20> kBandMinimumNorthingM{
    1100000.0, 2000000.0, 2800000.0, 3700000.0, 4600000.0,
    5500000.0, 6400000.0, 7300000.0, 8200000.0, 9100000.0,
          0.0,  800000.0, 1700000.0, 2600000.0, 3500000.0,
    4400000.0, 5300000.0, 6200000.0, 7000000.0, 7900000.0
};

std::string normalizedMgrs(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const unsigned char ch : text) {
        if (std::isspace(ch)) continue;
        normalized.push_back(static_cast<char>(std::toupper(ch)));
    }
    return normalized;
}

int letterIndex(const char* letters, char value) {
    const std::string sequence(letters);
    const auto pos = sequence.find(value);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

double powerOfTen(int exponent) {
    double value = 1.0;
    while (exponent-- > 0) value *= 10.0;
    return value;
}

bool inverseUtm(int zone,
                bool northernHemisphere,
                double eastingM,
                double northingM,
                double& latitudeDeg,
                double& longitudeDeg) {
    if (zone < 1 || zone > 60 ||
        eastingM < 100000.0 || eastingM >= 1000000.0 ||
        northingM < 0.0 || northingM > 10000000.0) {
        return false;
    }

    const double eccentricityPrimeSq =
        kWgs84EccentricitySq / (1.0 - kWgs84EccentricitySq);
    const double e1 =
        (1.0 - std::sqrt(1.0 - kWgs84EccentricitySq)) /
        (1.0 + std::sqrt(1.0 - kWgs84EccentricitySq));

    const double x = eastingM - 500000.0;
    double y = northingM;
    if (!northernHemisphere) y -= 10000000.0;

    const double meridionalArc = y / kUtmScale;
    const double mu = meridionalArc /
        (kWgs84SemiMajorM *
         (1.0 - kWgs84EccentricitySq / 4.0
          - 3.0 * std::pow(kWgs84EccentricitySq, 2.0) / 64.0
          - 5.0 * std::pow(kWgs84EccentricitySq, 3.0) / 256.0));

    const double footpointLat =
        mu
        + (3.0 * e1 / 2.0 - 27.0 * std::pow(e1, 3.0) / 32.0) * std::sin(2.0 * mu)
        + (21.0 * e1 * e1 / 16.0 - 55.0 * std::pow(e1, 4.0) / 32.0) * std::sin(4.0 * mu)
        + (151.0 * std::pow(e1, 3.0) / 96.0) * std::sin(6.0 * mu)
        + (1097.0 * std::pow(e1, 4.0) / 512.0) * std::sin(8.0 * mu);

    const double sinFootpoint = std::sin(footpointLat);
    const double cosFootpoint = std::cos(footpointLat);
    const double tanFootpoint = std::tan(footpointLat);
    const double n1 = kWgs84SemiMajorM /
        std::sqrt(1.0 - kWgs84EccentricitySq * sinFootpoint * sinFootpoint);
    const double r1 = kWgs84SemiMajorM * (1.0 - kWgs84EccentricitySq) /
        std::pow(1.0 - kWgs84EccentricitySq * sinFootpoint * sinFootpoint, 1.5);
    const double t1 = tanFootpoint * tanFootpoint;
    const double c1 = eccentricityPrimeSq * cosFootpoint * cosFootpoint;
    const double d = x / (n1 * kUtmScale);

    const double latitudeRad = footpointLat -
        (n1 * tanFootpoint / r1) *
        (d * d / 2.0
         - (5.0 + 3.0 * t1 + 10.0 * c1 - 4.0 * c1 * c1
            - 9.0 * eccentricityPrimeSq) * std::pow(d, 4.0) / 24.0
         + (61.0 + 90.0 * t1 + 298.0 * c1 + 45.0 * t1 * t1
            - 252.0 * eccentricityPrimeSq - 3.0 * c1 * c1) *
           std::pow(d, 6.0) / 720.0);

    const double longitudeOffsetRad =
        (d
         - (1.0 + 2.0 * t1 + c1) * std::pow(d, 3.0) / 6.0
         + (5.0 - 2.0 * c1 + 28.0 * t1 - 3.0 * c1 * c1
            + 8.0 * eccentricityPrimeSq + 24.0 * t1 * t1) *
           std::pow(d, 5.0) / 120.0) / cosFootpoint;

    latitudeDeg = latitudeRad * 180.0 / kPi;
    longitudeDeg = (zone * 6.0 - 183.0) + longitudeOffsetRad * 180.0 / kPi;
    return std::isfinite(latitudeDeg) && std::isfinite(longitudeDeg);
}

int utmZoneFor(double latitudeDeg, double longitudeDeg) {
    int zone = static_cast<int>(std::floor((longitudeDeg + 180.0) / 6.0)) + 1;
    zone = std::clamp(zone, 1, 60);
    if (latitudeDeg >= 56.0 && latitudeDeg < 64.0 &&
        longitudeDeg >= 3.0 && longitudeDeg < 12.0) {
        zone = 32;
    }
    if (latitudeDeg >= 72.0 && latitudeDeg < 84.0) {
        if (longitudeDeg >= 0.0 && longitudeDeg < 9.0) zone = 31;
        else if (longitudeDeg < 21.0) zone = 33;
        else if (longitudeDeg < 33.0) zone = 35;
        else if (longitudeDeg < 42.0) zone = 37;
    }
    return zone;
}

bool forwardUtm(double latitudeDeg,
                double longitudeDeg,
                int zone,
                double& eastingM,
                double& northingM) {
    if (latitudeDeg < -80.0 || latitudeDeg > 84.0 ||
        longitudeDeg < -180.0 || longitudeDeg > 180.0 ||
        zone < 1 || zone > 60) {
        return false;
    }

    const double latitudeRad = latitudeDeg * kPi / 180.0;
    const double longitudeRad = longitudeDeg * kPi / 180.0;
    const double centralMeridianRad = (zone * 6.0 - 183.0) * kPi / 180.0;
    const double eccentricityPrimeSq =
        kWgs84EccentricitySq / (1.0 - kWgs84EccentricitySq);
    const double sinLatitude = std::sin(latitudeRad);
    const double cosLatitude = std::cos(latitudeRad);
    const double tanLatitude = std::tan(latitudeRad);
    const double n = kWgs84SemiMajorM /
        std::sqrt(1.0 - kWgs84EccentricitySq * sinLatitude * sinLatitude);
    const double t = tanLatitude * tanLatitude;
    const double c = eccentricityPrimeSq * cosLatitude * cosLatitude;
    const double a = cosLatitude * (longitudeRad - centralMeridianRad);

    const double meridionalArc = kWgs84SemiMajorM *
        ((1.0 - kWgs84EccentricitySq / 4.0
          - 3.0 * std::pow(kWgs84EccentricitySq, 2.0) / 64.0
          - 5.0 * std::pow(kWgs84EccentricitySq, 3.0) / 256.0) * latitudeRad
         - (3.0 * kWgs84EccentricitySq / 8.0
            + 3.0 * std::pow(kWgs84EccentricitySq, 2.0) / 32.0
            + 45.0 * std::pow(kWgs84EccentricitySq, 3.0) / 1024.0) *
           std::sin(2.0 * latitudeRad)
         + (15.0 * std::pow(kWgs84EccentricitySq, 2.0) / 256.0
            + 45.0 * std::pow(kWgs84EccentricitySq, 3.0) / 1024.0) *
           std::sin(4.0 * latitudeRad)
         - (35.0 * std::pow(kWgs84EccentricitySq, 3.0) / 3072.0) *
           std::sin(6.0 * latitudeRad));

    eastingM = kUtmScale * n *
        (a + (1.0 - t + c) * std::pow(a, 3.0) / 6.0
         + (5.0 - 18.0 * t + t * t + 72.0 * c
            - 58.0 * eccentricityPrimeSq) * std::pow(a, 5.0) / 120.0)
        + 500000.0;
    northingM = kUtmScale *
        (meridionalArc + n * tanLatitude *
         (a * a / 2.0
          + (5.0 - t + 9.0 * c + 4.0 * c * c) * std::pow(a, 4.0) / 24.0
          + (61.0 - 58.0 * t + t * t + 600.0 * c
             - 330.0 * eccentricityPrimeSq) * std::pow(a, 6.0) / 720.0));
    if (latitudeDeg < 0.0) northingM += 10000000.0;
    return std::isfinite(eastingM) && std::isfinite(northingM);
}

} // namespace

bool mgrsToWgs84(const std::string& text,
                 MgrsCoordinate& coordinate,
                 std::string& error) {
    coordinate = {};
    error.clear();
    const std::string value = normalizedMgrs(text);
    if (value.empty()) {
        error = "Enter an MGRS reference.";
        return false;
    }

    std::size_t cursor = 0;
    while (cursor < value.size() && cursor < 2 && std::isdigit(
               static_cast<unsigned char>(value[cursor]))) {
        ++cursor;
    }
    if (cursor == 0 || cursor >= value.size()) {
        error = "MGRS must start with a grid-zone number from 1 to 60.";
        return false;
    }

    int zone = 0;
    try {
        zone = std::stoi(value.substr(0, cursor));
    } catch (...) {
        error = "The MGRS grid-zone number is invalid.";
        return false;
    }
    if (zone < 1 || zone > 60) {
        error = "MGRS grid-zone number must be between 1 and 60.";
        return false;
    }
    if (value.size() < cursor + 3) {
        error = "MGRS needs a latitude band and a two-letter 100 km square.";
        return false;
    }

    const char band = value[cursor++];
    const int bandIndex = letterIndex(kLatitudeBands, band);
    if (bandIndex < 0) {
        error = "MGRS latitude band must be C-X (excluding I and O).";
        return false;
    }

    const char columnLetter = value[cursor++];
    const char rowLetter = value[cursor++];
    const int columnIndex = letterIndex(kColumnSets[(zone - 1) % 3], columnLetter);
    const int rowIndex = letterIndex(kRowSets[(zone - 1) % 2], rowLetter);
    if (columnIndex < 0 || rowIndex < 0) {
        error = "The 100 km square letters are not valid for this MGRS zone.";
        return false;
    }

    const std::string digits = value.substr(cursor);
    if (digits.size() > 10 || digits.size() % 2 != 0 ||
        !std::all_of(digits.begin(), digits.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        error = "MGRS numeric precision must contain 0, 2, 4, 6, 8, or 10 digits.";
        return false;
    }

    const int precision = static_cast<int>(digits.size() / 2);
    const double resolutionM = powerOfTen(5 - precision);
    const std::string eastingDigits = digits.substr(0, precision);
    const std::string northingDigits = digits.substr(precision);
    const double eastingOffset = eastingDigits.empty() ? 0.0 : std::stod(eastingDigits);
    const double northingOffset = northingDigits.empty() ? 0.0 : std::stod(northingDigits);

    // MGRS values denote the south-west corner of their precision square. For
    // AOI centring, use the square centre (0.5 m at full 1 m precision).
    double eastingM = (columnIndex + 1) * 100000.0
                    + eastingOffset * resolutionM + resolutionM * 0.5;
    double northingM = rowIndex * 100000.0
                     + northingOffset * resolutionM + resolutionM * 0.5;
    while (northingM < kBandMinimumNorthingM[bandIndex]) northingM += 2000000.0;

    const bool northern = band >= 'N';
    double latitude = 0.0;
    double longitude = 0.0;
    if (!inverseUtm(zone, northern, eastingM, northingM, latitude, longitude)) {
        error = "The MGRS reference resolves outside the UTM coordinate range.";
        return false;
    }

    const double bandMinimumLat = -80.0 + bandIndex * 8.0;
    const double bandMaximumLat = band == 'X' ? 84.0 : bandMinimumLat + 8.0;
    if (latitude < bandMinimumLat - 1.0e-7 || latitude > bandMaximumLat + 1.0e-7) {
        error = "The MGRS northing does not fall inside its latitude band.";
        return false;
    }

    coordinate.latitudeDeg = latitude;
    coordinate.longitudeDeg = longitude;
    coordinate.precisionDigits = precision;
    return true;
}

bool wgs84ToMgrs(double latitudeDeg,
                 double longitudeDeg,
                 int precisionDigits,
                 std::string& mgrs,
                 std::string& error) {
    mgrs.clear();
    error.clear();
    if (!std::isfinite(latitudeDeg) || !std::isfinite(longitudeDeg) ||
        latitudeDeg < -80.0 || latitudeDeg > 84.0 ||
        longitudeDeg < -180.0 || longitudeDeg > 180.0) {
        error = "WGS84 coordinate is outside the UTM MGRS latitude/longitude range.";
        return false;
    }
    if (precisionDigits < 0 || precisionDigits > 5) {
        error = "MGRS precision must be between 0 and 5 digits per axis.";
        return false;
    }

    const int zone = utmZoneFor(latitudeDeg, longitudeDeg);
    double eastingM = 0.0;
    double northingM = 0.0;
    if (!forwardUtm(latitudeDeg, longitudeDeg, zone, eastingM, northingM)) {
        error = "Could not project the WGS84 coordinate into its UTM zone.";
        return false;
    }

    const int bandIndex = std::clamp(
        static_cast<int>(std::floor((latitudeDeg + 80.0) / 8.0)), 0, 19);
    const int columnIndex = static_cast<int>(std::floor(eastingM / 100000.0)) - 1;
    const int rowIndex = static_cast<int>(std::floor(northingM / 100000.0)) % 20;
    if (columnIndex < 0 || columnIndex >= 8 || rowIndex < 0) {
        error = "Projected coordinate does not map to a valid MGRS 100 km square.";
        return false;
    }

    std::ostringstream stream;
    stream << zone << kLatitudeBands[bandIndex]
           << kColumnSets[(zone - 1) % 3][columnIndex]
           << kRowSets[(zone - 1) % 2][rowIndex];
    if (precisionDigits > 0) {
        const int resolutionM = static_cast<int>(powerOfTen(5 - precisionDigits));
        const int divisor = std::max(resolutionM, 1);
        const int eastingDigits =
            static_cast<int>(std::floor(std::fmod(eastingM, 100000.0))) / divisor;
        const int northingDigits =
            static_cast<int>(std::floor(std::fmod(northingM, 100000.0))) / divisor;
        stream << std::setfill('0') << std::setw(precisionDigits) << eastingDigits
               << std::setw(precisionDigits) << northingDigits;
    }
    mgrs = stream.str();
    return true;
}
