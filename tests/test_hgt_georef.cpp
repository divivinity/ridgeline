#include "data/TerrainLoader.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace {
constexpr int kSamples = 1201;
constexpr std::size_t kSampleCount = static_cast<std::size_t>(kSamples) * kSamples;

bool writeZeroSrtm3(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const std::uint16_t zero = 0;
    for (std::size_t i = 0; i < kSampleCount; ++i) {
        out.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
        if (!out) return false;
    }
    return true;
}

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}
} // namespace

int main() {
    const std::string standard = "N35W079.hgt";
    const std::string renamed = "terrain.hgt";

    if (!writeZeroSrtm3(standard))
        return fail("could not create SRTM3 fixture");

    try {
        TerrainModel named = TerrainLoader::fromHGT(standard, 35.5f, -78.5f, 32);
        if (!named.hasGeoReference()) {
            std::remove(standard.c_str());
            return fail("standard SRTM filename should be georeferenced");
        }

        const auto southwest = named.worldToLatLon(0.0f, 0.0f);
        if (southwest.latDeg < 35.0 || southwest.latDeg > 36.0 ||
            southwest.lonDeg < -79.0 || southwest.lonDeg > -78.0) {
            std::remove(standard.c_str());
            return fail("standard SRTM filename produced an invalid WGS84 origin");
        }

        if (std::rename(standard.c_str(), renamed.c_str()) != 0) {
            std::remove(standard.c_str());
            return fail("could not rename SRTM fixture");
        }

        TerrainModel unknown = TerrainLoader::fromHGT(renamed, 35.5f, -78.5f, 32);
        std::remove(renamed.c_str());

        if (unknown.hasGeoReference())
            return fail("renamed HGT without an encoded tile origin must remain ungeoreferenced");
    } catch (const std::exception& e) {
        std::remove(standard.c_str());
        std::remove(renamed.c_str());
        std::cerr << "FAIL: unexpected exception: " << e.what() << '\n';
        return 1;
    }

    std::cout << "PASS: HGT georeference guard\n";
    return 0;
}
