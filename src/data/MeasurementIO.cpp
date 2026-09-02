#include "data/MeasurementIO.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace {

struct CsvRecord {
    std::vector<std::string> fields;
    std::size_t line = 0;
    std::string error;
};

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string normalizedHeader(std::string value) {
    value = trim(value);
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xef &&
        static_cast<unsigned char>(value[1]) == 0xbb &&
        static_cast<unsigned char>(value[2]) == 0xbf) {
        value.erase(0, 3);
    }
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool allBlank(const CsvRecord& record) {
    return std::all_of(record.fields.begin(), record.fields.end(),
                       [](const std::string& field) {
                           return trim(field).empty();
                       });
}

bool isComment(const CsvRecord& record) {
    return !record.fields.empty() &&
           !trim(record.fields.front()).empty() &&
           trim(record.fields.front()).front() == '#';
}

// Reads one RFC-4180-style record, including escaped quotes and newlines inside
// quoted fields. The record line is the physical line on which it begins.
bool readRecord(std::istream& input, std::size_t& nextLine, CsvRecord& record) {
    record = {};
    record.line = nextLine;
    std::string field;
    bool gotAny = false;
    bool inQuotes = false;
    bool afterQuote = false;

    char ch = '\0';
    while (input.get(ch)) {
        gotAny = true;
        if (inQuotes) {
            if (ch == '"') {
                if (input.peek() == '"') {
                    input.get(ch);
                    field.push_back('"');
                } else {
                    inQuotes = false;
                    afterQuote = true;
                }
            } else if (ch == '\r') {
                if (input.peek() == '\n') input.get(ch);
                field.push_back('\n');
                ++nextLine;
            } else {
                field.push_back(ch);
                if (ch == '\n') ++nextLine;
            }
            continue;
        }

        if (afterQuote) {
            if (ch == ',') {
                record.fields.push_back(field);
                field.clear();
                afterQuote = false;
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                if (ch == '\r' && input.peek() == '\n') input.get(ch);
                record.fields.push_back(field);
                ++nextLine;
                return true;
            }
            if (std::isspace(static_cast<unsigned char>(ch))) continue;
            if (record.error.empty())
                record.error = "Unexpected text after a closing quote";
            field.push_back(ch);
            afterQuote = false;
            continue;
        }

        if (ch == ',') {
            record.fields.push_back(field);
            field.clear();
        } else if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && input.peek() == '\n') input.get(ch);
            record.fields.push_back(field);
            ++nextLine;
            return true;
        } else if (ch == '"') {
            if (trim(field).empty()) {
                field.clear();
                inQuotes = true;
            } else {
                if (record.error.empty())
                    record.error = "Unexpected quote in an unquoted field";
                field.push_back(ch);
            }
        } else {
            field.push_back(ch);
        }
    }

    if (!gotAny && field.empty() && record.fields.empty()) return false;
    record.fields.push_back(field);
    if (inQuotes && record.error.empty())
        record.error = "Unterminated quoted field";
    return true;
}

bool parseNumber(const std::string& field, double& value) {
    const std::string text = trim(field);
    if (text.empty()) return false;
    try {
        std::size_t consumed = 0;
        value = std::stod(text, &consumed);
        return consumed == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

std::string rangeMessage(const char* name, const char* range) {
    std::ostringstream message;
    message << name << " must be a finite number in " << range;
    return message.str();
}

} // namespace

namespace MeasurementIO {

MeasurementLoadResult parseCsv(std::istream& input) {
    MeasurementLoadResult result;
    CsvRecord header;
    std::size_t nextLine = 1;
    bool foundHeader = false;
    while (readRecord(input, nextLine, header)) {
        if (allBlank(header) || isComment(header)) continue;
        foundHeader = true;
        break;
    }
    if (!foundHeader) {
        result.errors.push_back({1, "Measurement CSV is empty"});
        return result;
    }
    if (!header.error.empty()) {
        result.errors.push_back({header.line, header.error});
        return result;
    }

    std::map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < header.fields.size(); ++i) {
        const std::string name = normalizedHeader(header.fields[i]);
        if (name.empty()) {
            result.errors.push_back({header.line, "Header names cannot be empty"});
            return result;
        }
        if (!columns.emplace(name, i).second) {
            result.errors.push_back(
                {header.line, "Duplicate header column: " + name});
            return result;
        }
    }

    const auto indexOf = [&columns](const char* name) {
        const auto found = columns.find(name);
        return found == columns.end()
            ? std::numeric_limits<std::size_t>::max() : found->second;
    };
    const std::size_t latitude = indexOf("latitude");
    const std::size_t longitude = indexOf("longitude");
    const std::size_t xM = indexOf("x_m");
    const std::size_t zM = indexOf("z_m");
    const std::size_t measured = indexOf("measured_dbm");
    const std::size_t frequency = indexOf("frequency_mhz");
    const std::size_t height = indexOf("height_agl_m");
    const std::size_t label = indexOf("label");
    const std::size_t missing = std::numeric_limits<std::size_t>::max();

    const bool hasLatitude = latitude != missing;
    const bool hasLongitude = longitude != missing;
    const bool hasX = xM != missing;
    const bool hasZ = zM != missing;
    if (measured == missing) {
        result.errors.push_back(
            {header.line, "Required header measured_dbm is missing"});
        return result;
    }
    if (hasLatitude != hasLongitude) {
        result.errors.push_back(
            {header.line, "latitude and longitude headers must be provided together"});
        return result;
    }
    if (hasX != hasZ) {
        result.errors.push_back(
            {header.line, "x_m and z_m headers must be provided together"});
        return result;
    }
    if (!hasLatitude && !hasX) {
        result.errors.push_back({
            header.line,
            "Provide either latitude/longitude or x_m/z_m coordinate headers"});
        return result;
    }

    CsvRecord record;
    while (readRecord(input, nextLine, record)) {
        if (allBlank(record) || isComment(record)) continue;
        ++result.rowsRead;
        const auto reject = [&result, &record](std::string message) {
            ++result.rejectedRows;
            result.errors.push_back({record.line, std::move(message)});
        };
        if (!record.error.empty()) {
            reject(record.error);
            continue;
        }
        if (record.fields.size() > header.fields.size()) {
            reject("Row has more fields than the header");
            continue;
        }
        record.fields.resize(header.fields.size());
        const auto cell = [&record](std::size_t index) -> const std::string& {
            return record.fields[index];
        };

        const bool rowHasWgs = hasLatitude &&
            (!trim(cell(latitude)).empty() || !trim(cell(longitude)).empty());
        const bool rowHasLocal = hasX &&
            (!trim(cell(xM)).empty() || !trim(cell(zM)).empty());
        if (rowHasWgs && rowHasLocal) {
            reject("Row is ambiguous: populate either WGS84 or local coordinates, not both");
            continue;
        }
        if (!rowHasWgs && !rowHasLocal) {
            reject("Row has no coordinates");
            continue;
        }

        RFMeasurement measurement;
        measurement.sourceLine = record.line;
        if (!parseNumber(cell(measured), measurement.measuredDbm) ||
            measurement.measuredDbm < -300.0 || measurement.measuredDbm > 100.0) {
            reject(rangeMessage("measured_dbm", "[-300, 100] dBm"));
            continue;
        }

        if (rowHasWgs) {
            measurement.coordinateKind = MeasurementCoordinateKind::Wgs84;
            if (!parseNumber(cell(latitude), measurement.latitudeDeg) ||
                measurement.latitudeDeg < -90.0 || measurement.latitudeDeg > 90.0) {
                reject(rangeMessage("latitude", "[-90, 90] degrees"));
                continue;
            }
            if (!parseNumber(cell(longitude), measurement.longitudeDeg) ||
                measurement.longitudeDeg < -180.0 || measurement.longitudeDeg > 180.0) {
                reject(rangeMessage("longitude", "[-180, 180] degrees"));
                continue;
            }
        } else {
            measurement.coordinateKind = MeasurementCoordinateKind::LocalXZ;
            if (!parseNumber(cell(xM), measurement.xM) ||
                std::abs(measurement.xM) > 1.0e9) {
                reject(rangeMessage("x_m", "[-1e9, 1e9] metres"));
                continue;
            }
            if (!parseNumber(cell(zM), measurement.zM) ||
                std::abs(measurement.zM) > 1.0e9) {
                reject(rangeMessage("z_m", "[-1e9, 1e9] metres"));
                continue;
            }
        }

        if (frequency != missing && !trim(cell(frequency)).empty()) {
            if (!parseNumber(cell(frequency), measurement.frequencyMHz) ||
                measurement.frequencyMHz <= 0.0 ||
                measurement.frequencyMHz > 1.0e6) {
                reject(rangeMessage("frequency_mhz", "(0, 1e6] MHz"));
                continue;
            }
        }
        if (height != missing && !trim(cell(height)).empty()) {
            if (!parseNumber(cell(height), measurement.heightAglM) ||
                measurement.heightAglM < 0.0 || measurement.heightAglM > 100000.0) {
                reject(rangeMessage("height_agl_m", "[0, 100000] metres"));
                continue;
            }
        }
        if (label != missing) measurement.label = cell(label);
        result.measurements.push_back(std::move(measurement));
    }

    if (result.measurements.empty() && result.rowsRead > 0) {
        result.warnings.push_back(
            {header.line, "No usable measurement rows were imported"});
    }
    return result;
}

MeasurementLoadResult loadCsv(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        MeasurementLoadResult result;
        result.errors.push_back({0, "Cannot open measurement CSV: " + path});
        return result;
    }
    return parseCsv(file);
}

} // namespace MeasurementIO
