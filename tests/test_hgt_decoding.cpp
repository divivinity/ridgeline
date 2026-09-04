// HGT sample decoding.
//
// test_hgt_georef.cpp covers where a tile sits on the globe, using a fixture
// filled with a single constant elevation. That leaves how each sample is
// decoded untested: byte-swapping a constant of zero yields zero, so a broken
// endian conversion passes, as would a lost void sentinel or a flipped row
// order. These tests use a fixture with distinct values at known positions so
// each of those is pinned down.
#include "data/TerrainLoader.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int g_fail = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; \
        ++g_fail; \
    } \
} while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::abs((double)(a) - (double)(b)) <= (double)(tol))

namespace {

constexpr int kSamples = 1201;  // SRTM3: 1201 * 1201 * 2 == 2884802 bytes

// Writes a big-endian SRTM3 tile. Every sample is kFill except the two the
// caller names, so decoding errors cannot hide behind a uniform value.
bool writeSrtm3(const std::string& path, std::int16_t fill,
                std::int16_t northWest, std::int16_t southWest) {
    std::vector<char> bytes(static_cast<std::size_t>(kSamples) * kSamples * 2);
    auto put = [&bytes](int row, int col, std::int16_t value) {
        const std::size_t i = (static_cast<std::size_t>(row) * kSamples + col) * 2;
        const auto raw = static_cast<std::uint16_t>(value);
        bytes[i]     = static_cast<char>((raw >> 8) & 0xff);  // big-endian
        bytes[i + 1] = static_cast<char>(raw & 0xff);
    };
    for (int r = 0; r < kSamples; ++r)
        for (int c = 0; c < kSamples; ++c)
            put(r, c, fill);
    put(0, 0, northWest);              // HGT row 0 is the NORTH edge
    put(kSamples - 1, 0, southWest);

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

// Whole tile, so the crop starts at row/col 0 and the marked corners are the
// corners of the returned model.
TerrainModel loadWholeTile(const std::string& path) {
    return TerrainLoader::fromHGT(path, 40.5f, -74.5f, kSamples);
}

void test_big_endian_samples_are_decoded() {
    const std::string path = "N40W075.hgt";
    if (!writeSrtm3(path, 100, 1000, 250)) { CHECK(false); return; }

    const TerrainModel t = loadWholeTile(path);
    CHECK(t.width == kSamples);
    CHECK(t.height == kSamples);
    // 100 is 0x0064; read without the swap it would come back as 0x6400.
    CHECK_NEAR(t.heightAt(600, 600), 100.0f, 1e-3);

    std::remove(path.c_str());
}

void test_north_south_rows_are_flipped() {
    const std::string path = "N40W075.hgt";
    if (!writeSrtm3(path, 100, 1000, 250)) { CHECK(false); return; }

    const TerrainModel t = loadWholeTile(path);
    // HGT row 0 is north; TerrainModel row height-1 is north.
    CHECK_NEAR(t.heightAt(0, t.height - 1), 1000.0f, 1e-3);
    CHECK_NEAR(t.heightAt(0, 0), 250.0f, 1e-3);
    // Latitude must increase with world Z.
    const auto south = t.worldToLatLon(0.0f, 0.0f);
    const auto north = t.worldToLatLon(0.0f, t.worldSize().z);
    CHECK(north.latDeg > south.latDeg);

    std::remove(path.c_str());
}

void test_void_samples_become_zero() {
    const std::string path = "N40W075.hgt";
    // -32768 marks a void; it must not surface as a -32768 m elevation.
    if (!writeSrtm3(path, 100, 1000, -32768)) { CHECK(false); return; }

    const TerrainModel t = loadWholeTile(path);
    CHECK_NEAR(t.heightAt(0, 0), 0.0f, 1e-3);
    for (float h : t.heights)
        if (h < -1000.0f) { CHECK(false); break; }

    std::remove(path.c_str());
}

void test_crop_size_is_clamped() {
    const std::string path = "N40W075.hgt";
    if (!writeSrtm3(path, 100, 1000, 250)) { CHECK(false); return; }

    const TerrainModel tooBig   = TerrainLoader::fromHGT(path, 40.5f, -74.5f, 99999);
    const TerrainModel tooSmall = TerrainLoader::fromHGT(path, 40.5f, -74.5f, 0);
    CHECK(tooBig.width == kSamples);
    CHECK(tooSmall.width == 2);
    CHECK(tooSmall.heights.size() == 4u);

    std::remove(path.c_str());
}

void test_wrong_sized_file_throws() {
    // Neither SRTM1 nor SRTM3 sized: reject it rather than read past the data.
    const std::string path = "truncated.hgt";
    { std::ofstream out(path, std::ios::binary); out.write("not a tile", 10); }

    bool threw = false;
    try { TerrainLoader::fromHGT(path); }
    catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    std::remove(path.c_str());
}

void test_missing_file_throws() {
    bool threw = false;
    try { TerrainLoader::fromHGT("no_such_tile.hgt"); }
    catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}

}  // namespace

int main() {
    test_big_endian_samples_are_decoded();
    test_north_south_rows_are_flipped();
    test_void_samples_become_zero();
    test_crop_size_is_clamped();
    test_wrong_sized_file_throws();
    test_missing_file_throws();

    if (g_fail == 0)
        std::cout << "HGT decoding tests passed.\n";
    return g_fail ? 1 : 0;
}
