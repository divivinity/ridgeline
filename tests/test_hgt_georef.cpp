#include "data/TerrainLoader.h"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int kSamples = 1201;

bool writeConstantSrtm3(const std::string& path, std::int16_t elevation) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const std::uint16_t raw = static_cast<std::uint16_t>(elevation);
    const char hi = static_cast<char>((raw >> 8) & 0xff);
    const char lo = static_cast<char>(raw & 0xff);
    std::vector<char> row(static_cast<std::size_t>(kSamples) * 2);
    for (int i = 0; i < kSamples; ++i) {
        row[static_cast<std::size_t>(i) * 2] = hi;
        row[static_cast<std::size_t>(i) * 2 + 1] = lo;
    }
    for (int r = 0; r < kSamples; ++r) {
        out.write(row.data(), static_cast<std::streamsize>(row.size()));
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
    const std::string southernWestern = "S12W077.hgt";
    const std::string seamDir = "hgt_seam_fixture";

    if (!writeConstantSrtm3(standard, 0))
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

        if (!writeConstantSrtm3(southernWestern, 0))
            return fail("could not create southern/western SRTM fixture");
        TerrainModel swHemisphere = TerrainLoader::fromHGT(
            southernWestern, -11.5f, -76.5f, 32);
        std::remove(southernWestern.c_str());
        if (!swHemisphere.hasGeoReference())
            return fail("southern/western SRTM filename should be georeferenced");
        const auto sw2 = swHemisphere.worldToLatLon(0.0f, 0.0f);
        if (sw2.latDeg < -12.0 || sw2.latDeg > -11.0 ||
            sw2.lonDeg < -77.0 || sw2.lonDeg > -76.0)
            return fail("southern/western HGT origin parsed incorrectly");

        std::filesystem::create_directory(seamDir);
        const std::string westTile = seamDir + "/N35W079.hgt";
        const std::string eastTile = seamDir + "/N35W078.hgt";
        if (!writeConstantSrtm3(westTile, 100) ||
            !writeConstantSrtm3(eastTile, 200))
            return fail("could not create HGT seam fixtures");

        TerrainModel seam = TerrainLoader::fromHGTDirectory(
            seamDir, 35.5, -78.0, 12.0, 256);
        if (!seam.hasGeoReference())
            return fail("HGT seam mosaic lost its georeference");
        const float midZ = seam.worldSize().z * 0.5f;
        const float westH = seam.heightAtWorld(seam.worldSize().x * 0.1f, midZ);
        const float eastH = seam.heightAtWorld(seam.worldSize().x * 0.9f, midZ);
        if (!(westH < 130.0f && eastH > 170.0f))
            return fail("HGT seam mosaic did not preserve adjacent tile elevations");
        const auto center = seam.worldToLatLon(
            seam.worldSize().x * 0.5f, seam.worldSize().z * 0.5f);
        if (std::abs(center.lonDeg - (-78.0)) > 0.02)
            return fail("HGT seam mosaic was not centred on the requested longitude");

        std::filesystem::remove_all(seamDir);
    } catch (const std::exception& e) {
        std::remove(standard.c_str());
        std::remove(renamed.c_str());
        std::remove(southernWestern.c_str());
        std::filesystem::remove_all(seamDir);
        std::cerr << "FAIL: unexpected exception: " << e.what() << '\n';
        return 1;
    }

    std::cout << "PASS: HGT georeference and seam guards\n";
    return 0;
}
