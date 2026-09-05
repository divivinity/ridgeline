#include "AntennaPatternIO.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using Sample = std::pair<float, float>;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) fields.push_back(field);
    return fields;
}

float interpolate(const std::vector<Sample>& samples, float angle, bool circular) {
    if (samples.empty()) return 0.0f;
    if (samples.size() == 1) return samples.front().second;

    if (circular) {
        angle = std::fmod(angle, 360.0f);
        if (angle < 0.0f) angle += 360.0f;
        std::vector<Sample> extended = samples;
        extended.emplace_back(samples.front().first + 360.0f, samples.front().second);
        if (angle < extended.front().first) angle += 360.0f;
        for (std::size_t i = 1; i < extended.size(); ++i) {
            if (angle <= extended[i].first) {
                const float span = std::max(extended[i].first - extended[i-1].first, 1.0e-6f);
                const float t = (angle - extended[i-1].first) / span;
                return extended[i-1].second * (1.0f - t) + extended[i].second * t;
            }
        }
        return extended.back().second;
    }

    angle = std::clamp(angle, samples.front().first, samples.back().first);
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (angle <= samples[i].first) {
            const float span = std::max(samples[i].first - samples[i-1].first, 1.0e-6f);
            const float t = (angle - samples[i-1].first) / span;
            return samples[i-1].second * (1.0f - t) + samples[i].second * t;
        }
    }
    return samples.back().second;
}

void normalizeSamples(std::vector<Sample>& samples, bool circular) {
    for (auto& sample : samples) {
        if (circular) {
            sample.first = std::fmod(sample.first, 360.0f);
            if (sample.first < 0.0f) sample.first += 360.0f;
        } else {
            sample.first = std::clamp(sample.first, -90.0f, 90.0f);
        }
        sample.second = std::max(0.0f, sample.second);
    }
    std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) {
        return a.first < b.first;
    });
    samples.erase(std::unique(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) {
        return std::abs(a.first - b.first) < 1.0e-5f;
    }), samples.end());
}
} // namespace

void AntennaPatternIO::loadCsv(const std::string& path, Antenna& antenna) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open antenna pattern: " + path);

    std::vector<Sample> azimuth;
    std::vector<Sample> elevation;
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (line.empty() || line[0] == '#') continue;
        const auto fields = splitCsv(line);
        if (fields.size() < 2) continue;

        try {
            if (fields.size() >= 3) {
                const std::string plane = lower(fields[0]);
                if (plane == "plane" || plane == "axis") continue;
                const float angle = std::stof(fields[1]);
                const float attenuation = std::stof(fields[2]);
                if (plane.find("elev") != std::string::npos || plane == "el")
                    elevation.emplace_back(angle, attenuation);
                else if (plane.find("az") != std::string::npos)
                    azimuth.emplace_back(angle, attenuation);
            } else {
                const std::string first = lower(fields[0]);
                if (first.find("angle") != std::string::npos) continue;
                azimuth.emplace_back(std::stof(fields[0]), std::stof(fields[1]));
            }
        } catch (const std::exception&) {
            throw std::runtime_error(
                "Invalid antenna pattern CSV value at line " + std::to_string(lineNumber));
        }
    }

    if (azimuth.empty() && elevation.empty())
        throw std::runtime_error("Antenna pattern contains no usable samples");

    if (!azimuth.empty()) {
        normalizeSamples(azimuth, true);
        antenna.azimuthPatternDb.resize(360);
        for (int degree = 0; degree < 360; ++degree)
            antenna.azimuthPatternDb[degree] = interpolate(azimuth, static_cast<float>(degree), true);
    }
    if (!elevation.empty()) {
        normalizeSamples(elevation, false);
        antenna.elevationPatternDb.resize(181);
        for (int degree = -90; degree <= 90; ++degree)
            antenna.elevationPatternDb[degree + 90] = interpolate(
                elevation, static_cast<float>(degree), false);
    }
    antenna.patternSource = path;
}
