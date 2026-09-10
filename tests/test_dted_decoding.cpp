#include "data/TerrainLoader.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
constexpr int start = 3428, record = 18;
void put16(Bytes& b, int at, unsigned value) {
    b[at] = value >> 8; b[at + 1] = value & 255;
}
void checksum(Bytes& b, int col) {
    const int at = start + col * record;
    unsigned sum = 0;
    for (int i = 0; i < record - 4; ++i) sum += b[at + i];
    for (int i = 0; i < 4; ++i) b[at + record - 4 + i] = (sum >> (24 - 8*i)) & 255;
}
void field(Bytes& b, int at, const std::string& text) {
    std::copy(text.begin(), text.end(), b.begin() + at);
}
Bytes fixture() {
    Bytes b(start + 2 * record, ' ');
    field(b, 0, "UHL1"); field(b, 4, "0100000W"); field(b, 12, "0450000S");
    field(b, 20, "0020"); field(b, 24, "0010");
    field(b, 47, "0002"); field(b, 51, "0003"); b[55] = '0';
    field(b, 80, "DSI"); field(b, 224, "WGS84"); field(b, 728, "ACC");
    const unsigned samples[2][3] = {{0x800c, 0x8000, 35}, {100, 0x8001, 32767}};
    for (int col = 0; col < 2; ++col) {
        const int at = start + col * record;
        std::fill(b.begin() + at, b.begin() + at + record, 0);
        b[at] = 0xaa; b[at + 3] = col; put16(b, at + 4, col);
        for (int row = 0; row < 3; ++row) put16(b, at + 8 + row * 2, samples[col][row]);
        checksum(b, col);
    }
    return b;
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Temp {
    std::filesystem::path dir;
    Temp() : dir(std::filesystem::temp_directory_path() /
        ("ridgeline-dted-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        require(std::filesystem::create_directory(dir), "create fixture directory");
    }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    TerrainModel load(const Bytes& bytes) {
        const auto path = dir / std::filesystem::u8path("\xe5\x9c\xb0\xe5\xbd\xa2-\xc3\xa9.dt1");
        { std::ofstream out(path, std::ios::binary);
          out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
          require(bool(out), "write fixture"); }
        return TerrainLoader::fromDTED(path.u8string());
    }
};
}
int main(int argc, char** argv) {
    try {
        Temp temp;
        const auto good = fixture();
        const auto t = temp.load(good);
        require(t.width == 2 && t.height == 3, "standard UHL dimensions");
        require(t.heights == std::vector<float>({-12, 100, 0, -1, 35, 32767}), "sign magnitude / orientation");
        require(t.originLatDeg == -45 && t.originLonDeg == -10, "southern/western origin");
        const double center = -45 + 1.0 / 3600;
        require(std::abs(t.metersPerCellX() - TerrainModel::metersPerDegreeLon(center)*2/3600) < 0.001, "longitude spacing");
        require(std::abs(t.metersPerCellZ() - TerrainModel::metersPerDegreeLat(center)/3600) < 0.001, "latitude spacing");
        auto northeast = good; field(northeast, 4, "0100000E"); field(northeast, 12, "0450000N");
        require(temp.load(northeast).originLatDeg == 45, "northern origin");
        require(temp.load(northeast).originLonDeg == 10, "eastern origin");
        // Pin geographic registration at every sample, not only the SW origin.
        for (int row = 0; row < t.height; ++row) {
            for (int col = 0; col < t.width; ++col) {
                const auto geo = t.worldToLatLon(col * t.metersPerCellX(), row * t.metersPerCellZ());
                require(std::abs(geo.latDeg - (-45 + row / 3600.0)) < 1e-9, "sample latitude registration");
                require(std::abs(geo.lonDeg - (-10 + col * 2 / 3600.0)) < 1e-9, "sample longitude registration");
                const auto world = t.latLonToWorld(geo.latDeg, geo.lonDeg);
                require(std::abs(world.x - col * t.metersPerCellX()) < 0.001, "longitude round trip");
                require(std::abs(world.y - row * t.metersPerCellZ()) < 0.001, "latitude round trip");
            }
        }
        auto rejectWith = [&](const Bytes& b, const std::string& expected) {
            try { temp.load(b); }
            catch (const std::runtime_error& e) {
                if (std::string(e.what()).find(expected) != std::string::npos) return;
                throw std::runtime_error("Expected diagnostic '" + expected + "', got: " + e.what());
            }
            throw std::runtime_error("Expected rejection: " + expected);
        };
        // Each profile and row must be checked; a generic parse failure must not
        // accidentally satisfy a missing-data or checksum regression.
        for (int col = 0; col < 2; ++col) {
            for (int row = 0; row < 3; ++row) {
                auto missing = good;
                put16(missing, start + col * record + 8 + row * 2, 0xffff);
                checksum(missing, col);
                rejectWith(missing, "missing elevation at column " + std::to_string(col)
                    + ", row " + std::to_string(row));
            }
            auto corrupt = good;
            corrupt[start + col * record + record - 1] ^= 1;
            rejectWith(corrupt, "checksum mismatch at column " + std::to_string(col));
        }
        auto shortHeader = good; shortHeader.resize(79);
        auto otherDatum = good; field(otherDatum, 224, "WGS72");
        rejectWith(otherDatum, "unsupported or missing horizontal datum");
        field(otherDatum, 224, "     ");
        rejectWith(otherDatum, "unsupported or missing horizontal datum");
        rejectWith(shortHeader, "missing UHL1");
        auto shortMetadata = good; shortMetadata.resize(start - 1);
        rejectWith(shortMetadata, "missing DSI or ACC");
        auto badSeconds = good; field(badSeconds, 4, "0100060E");
        rejectWith(badSeconds, "origin outside coordinate bounds");
        auto eastOverflow = good; field(eastOverflow, 4, "1800000E");
        rejectWith(eastOverflow, "grid extent outside coordinate bounds");
        // Exact eastern boundary is valid, and maps to normalized -180 degrees.
        auto eastBoundary = good; field(eastBoundary, 4, "1795958E");
        const auto edge = temp.load(eastBoundary);
        require(std::abs(edge.worldToLatLon(edge.metersPerCellX(), 0).lonDeg + 180) < 1e-9,
            "exact antimeridian endpoint");
        auto reject = [&](Bytes b, const char* label) {
            bool threw = false;
            try { temp.load(b); } catch (const std::runtime_error&) { threw = true; }
            require(threw, label);
        };
        auto b = good; put16(b, start + 8, 0xffff); checksum(b, 0); reject(b, "void rejected");
        b = good; b[start + 9] ^= 1; reject(b, "checksum rejected");
        b = good; b.pop_back(); reject(b, "truncation rejected");
        b = good; b[start] = 0; checksum(b, 0); reject(b, "sentinel rejected");
        b = good; put16(b, start + 4, 1); checksum(b, 0); reject(b, "column order rejected");
        b = good; put16(b, start + 6, 1); checksum(b, 0); reject(b, "latitude index rejected");
        b = good; b[80] = 'X'; reject(b, "DSI rejected");
        b = good; b[728] = 'X'; reject(b, "ACC rejected");
        b = good; field(b, 20, "001x"); reject(b, "nondigit rejected");
        b = good; field(b, 20, "0000"); reject(b, "zero interval rejected");
        b = good; field(b, 47, "9999"); reject(b, "oversized truncated grid rejected");
        b = good; field(b, 12, "0456000S"); reject(b, "bad minutes rejected");
        b = good; field(b, 12, "0910000N"); reject(b, "bad latitude rejected");
        b = good; field(b, 12, "0450000E"); reject(b, "wrong hemisphere rejected");
        b = good; field(b, 12, "0900000N"); reject(b, "out of bounds extent rejected");
        if (argc > 1) {
            // Optional GDAL n43.dt0 reference, never downloaded by the test.
            // GDAL's pixel-area geotransform has half-cell outer edges; our
            // sample reference is the southwest sample center at 43N, 80W.
            const auto reference = TerrainLoader::fromDTED(argv[1]);
            require(reference.width == 121 && reference.height == 121, "reference dimensions");
            require(std::abs(reference.originLatDeg - 43) < 1e-9 &&
                    std::abs(reference.originLonDeg + 80) < 1e-9, "reference southwest sample");
            require(std::abs(reference.cellLatDeg - 1.0/120) < 1e-12 &&
                    std::abs(reference.cellLonDeg - 1.0/120) < 1e-12, "reference sample interval");
            const auto corner = reference.worldToLatLon(reference.worldSize().x, reference.worldSize().z);
            require(std::abs(corner.latDeg - 44) < 1e-6 && std::abs(corner.lonDeg + 79) < 1e-6,
                    "reference northeast sample");
            // Match GDALChecksumImage's integer modulo checksum in north-first
            // raster order, not Ridgeline's south-first storage order.
            // Algorithm: OSGeo/gdal alg/gdalchecksum.cpp; expected values:
            // autotest/gdrivers/dted.py test_dted_1 and test_dted_4.
            const auto rasterChecksum = [&](int x, int y, int width, int height) {
                constexpr int primes[] = {7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43};
                int sum = 0, index = 0;
                for (int row = y; row < y + height; ++row)
                    for (int col = x; col < x + width; ++col) {
                        const int value = static_cast<int>(reference.heightAt(col, reference.height - 1 - row));
                        sum = (sum + value % primes[index]) & 0xffff;
                        index = (index + 1) % 11;
                    }
                return sum;
            };
            // DTED posts are the source samples, so the post spacing is the
            // resolution. 30 arc-seconds is about 926 m north-south but only
            // about 673 m east-west at 43.5 N, and sourceResolutionM reports
            // the finer of the two axes rather than averaging them.
            require(reference.sourceResolutionM > 600.0f &&
                        reference.sourceResolutionM < 700.0f,
                    "reference reports its true 30 arc-second post spacing");
            require(rasterChecksum(0, 0, 121, 121) == 49187, "GDAL reference elevation checksum");
            require(rasterChecksum(5, 5, 5, 5) == 305, "GDAL reference subwindow checksum");
            std::cout << "GDAL n43 reference registration and elevation checksums passed\n";
        }
        if (argc > 2) {
            bool rejected = false;
            try { TerrainLoader::fromDTED(argv[2]); }
            catch (const std::runtime_error& e) {
                rejected = std::string(e.what()).find("unsupported or missing horizontal datum") != std::string::npos;
            }
            require(rejected, "GDAL WGS72 reference must not be labeled WGS84");
            std::cout << "GDAL WGS72 rejection passed\n";
        }
        std::cout << "DTED decoding checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
