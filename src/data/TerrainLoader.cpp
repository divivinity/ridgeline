#include "TerrainLoader.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// DTED parser
// ---------------------------------------------------------------------------
// UHL record layout (80 bytes, 0-based offsets):
//   0- 3  "UHL1"
//   4-11  Origin longitude  DDDMMSSH  (e.g. "0101230E")
//  12-19  Origin latitude   DDMMSSH_  (e.g. "45123N  ")  -- 7 sig chars + pad
//  20-23  Longitude interval, tenths of arc-seconds ("0100" = 10.0 → 1 arc-sec)
//  24-27  Latitude  interval, tenths of arc-seconds
//  28-31  Absolute vertical accuracy
//  32-34  Security code (3 chars)
//  35-46  Unique reference (12 chars)
//  47-50  Reserved
//  51-54  Number of longitude lines   (columns, west→east)
//  55-58  Number of latitude  points  (rows, south→north)
//  59     Multiple accuracy flag
//  60-79  Reserved
// Then: DSI 648 bytes, ACC 2700 bytes  → data starts at byte 3428.
//
// Data records (one per longitude column):
//   byte 0    : 0xAA sentinel
//   bytes 1-3 : block count (24-bit big-endian)
//   bytes 4-5 : longitude count (16-bit big-endian, 0-based column index)
//   bytes 6-7 : latitude  count (16-bit big-endian, always 0)
//   bytes 8.. : numLat × signed int16 big-endian elevations (south→north)
//   last 4    : checksum (not validated here)
// Void elevation = 0x8000 (-32768 signed) → replaced with 0 m.

static int parseInt4(const char* p) {
    char buf[5] = {};
    std::memcpy(buf, p, 4);
    return std::stoi(buf);
}

// Parse DDDMMSSH or DDMMSSH_ into total arc-seconds (signed: W/S = negative).
static int parseOriginArcSec(const char* p, int len) {
    // Find hemisphere: last non-space character
    char hemi = ' ';
    int sigLen = 0;
    for (int i = len - 1; i >= 0; --i) {
        if (p[i] == 'N' || p[i] == 'S' || p[i] == 'E' || p[i] == 'W') {
            hemi  = p[i];
            sigLen = i;   // digits occupy [0, sigLen)
            break;
        }
    }
    if (hemi == ' ' || sigLen < 6)
        throw std::runtime_error("DTED UHL: cannot parse origin coordinate");

    // sigLen-2 .. sigLen-1 = seconds, sigLen-4 .. sigLen-3 = minutes, rest = degrees
    char buf[8] = {};
    std::memcpy(buf, p + sigLen - 2, 2);  int sec = std::stoi(buf);
    std::memcpy(buf, p + sigLen - 4, 2);  int min = std::stoi(buf);
    std::memset(buf, 0, sizeof(buf));
    std::memcpy(buf, p, sigLen - 4);      int deg = std::stoi(buf);

    int total = deg * 3600 + min * 60 + sec;
    return (hemi == 'W' || hemi == 'S') ? -total : total;
}

static int16_t readBE16(const char* p) {
    return static_cast<int16_t>(
        (static_cast<uint8_t>(p[0]) << 8) | static_cast<uint8_t>(p[1]));
}

TerrainModel TerrainLoader::fromDTED(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open DTED file: " + path);

    // --- UHL ---
    char uhl[80];
    f.read(uhl, 80);
    if (!f || std::strncmp(uhl, "UHL1", 4) != 0)
        throw std::runtime_error("Not a valid DTED file (missing UHL1): " + path);

    const int latIntervalTenths = parseInt4(uhl + 24);  // tenths of arc-sec
    const int numLon             = parseInt4(uhl + 51);  // columns
    const int numLat             = parseInt4(uhl + 55);  // rows per column

    if (numLon < 2 || numLat < 2 || latIntervalTenths <= 0)
        throw std::runtime_error("DTED UHL contains invalid grid dimensions");

    // Latitude of SW corner — used to compute cell size in metres.
    const int originLatSec = parseOriginArcSec(uhl + 12, 8);
    const double centerLatDeg =
        originLatSec / 3600.0 + (numLat * latIntervalTenths) / (2.0 * 36000.0);

    // 1 arc-second latitude ≈ 30.87 m (flat-earth approximation, fine for <50 km).
    const double metersPerArcSec = 30.87;
    const float  cellSizeM =
        static_cast<float>(metersPerArcSec * latIntervalTenths / 10.0);
    (void)centerLatDeg;   // available for future lon-scale correction

    // Skip DSI (648) + ACC (2700); data starts at byte 3428.
    f.seekg(80 + 648 + 2700, std::ios::beg);

    TerrainModel t;
    t.width    = numLon;
    t.height   = numLat;
    t.cellSize = cellSizeM;
    t.heights.assign(numLon * numLat, 0.0f);

    const int recordSize = 8 + numLat * 2 + 4;
    std::vector<char> rec(recordSize);

    for (int col = 0; col < numLon; ++col) {
        f.read(rec.data(), recordSize);
        if (!f)
            throw std::runtime_error(
                "Unexpected end of DTED file at column " + std::to_string(col));

        if (static_cast<uint8_t>(rec[0]) != 0xAA)
            throw std::runtime_error(
                "Invalid DTED data record sentinel at column " + std::to_string(col));

        for (int row = 0; row < numLat; ++row) {
            const int16_t raw = readBE16(rec.data() + 8 + row * 2);
            // 0x8000 (-32768) is the DTED void/unknown elevation indicator.
            const float h = (raw == -32768) ? 0.0f : static_cast<float>(raw);
            // DTED: col = west→east (x), row = south→north (z).
            t.heights[row * numLon + col] = h;
        }
    }

    return t;
}

// ---------------------------------------------------------------------------
// SRTM HGT parser
// ---------------------------------------------------------------------------
// Format: raw signed big-endian int16, row-major, north→south.
// SRTM1: 3601×3601 samples (25 934 402 bytes), ~30.87 m/cell at 35°N.
// SRTM3: 1201×1201 samples ( 2 884 802 bytes), ~92.6  m/cell at 35°N.
// Void value: -32768 → replaced with 0 m.
// Tile SW corner encoded in filename: N35W079.hgt → lat=35, lon=-79.

// Parse tile SW corner from HGT filename (e.g. "N35W079.hgt").
// Returns false if filename does not match expected pattern.
static bool parseHGTOrigin(const std::string& path, int& swLat, int& swLon) {
    size_t sep = path.find_last_of("/\\");
    std::string name = (sep == std::string::npos) ? path : path.substr(sep + 1);
    for (auto& c : name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    // Pattern: [NS]\d{2}[EW]\d{3}\.HGT
    if (name.size() < 7) return false;
    char latH = name[0];
    if (latH != 'N' && latH != 'S') return false;
    if (!std::isdigit(name[1]) || !std::isdigit(name[2])) return false;
    int absLat = (name[1] - '0') * 10 + (name[2] - '0');

    char lonH = name[3];
    if (lonH != 'E' && lonH != 'W') return false;
    if (!std::isdigit(name[4]) || !std::isdigit(name[5]) || !std::isdigit(name[6])) return false;
    int absLon = (name[4] - '0') * 100 + (name[5] - '0') * 10 + (name[6] - '0');

    swLat = (latH == 'N') ? absLat : -absLat;
    swLon = (lonH == 'E') ? absLon : -absLon;
    return true;
}

TerrainModel TerrainLoader::fromHGT(const std::string& path,
                                    float cropCenterLat,
                                    float cropCenterLon,
                                    int   cropSize) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open HGT file: " + path);

    // Detect resolution from file size.
    f.seekg(0, std::ios::end);
    const auto fileSize = static_cast<long long>(f.tellg());
    f.seekg(0, std::ios::beg);

    int samples;
    float metersPerCell;
    if (fileSize == 25934402LL) {
        samples        = 3601;
        metersPerCell  = 30.87f;   // 1 arc-sec latitude ≈ 30.87 m at 35°N
    } else if (fileSize == 2884802LL) {
        samples        = 1201;
        metersPerCell  = 92.6f;    // 3 arc-sec
    } else {
        throw std::runtime_error(
            "HGT file has unexpected size (expected SRTM1 or SRTM3): " + path);
    }

    // Read raw big-endian int16 data.
    const int total = samples * samples;
    std::vector<int16_t> raw(total);
    f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(total) * 2);
    if (!f)
        throw std::runtime_error("Failed to read HGT data from: " + path);

    // Swap bytes: big-endian → native (little-endian on x86).
    for (auto& v : raw) {
        const auto u = static_cast<uint16_t>(v);
        v = static_cast<int16_t>(((u & 0xFF00u) >> 8) | ((u & 0x00FFu) << 8));
    }

    // Derive tile SW corner from filename, or fall back to centre of file.
    int swLat = 0, swLon = 0;
    const bool haveOrigin = parseHGTOrigin(path, swLat, swLon);

    // Compute centre of crop in tile coordinates.
    // HGT row 0 = northern edge (swLat + 1 °), row (samples-1) = southern edge (swLat).
    // HGT col 0 = western  edge (swLon),        col (samples-1) = eastern  edge (swLon + 1 °).
    int centerRow, centerCol;
    if (haveOrigin) {
        // Convert geographic coords to tile row/col.
        const float nEdge = static_cast<float>(swLat + 1);   // northern edge latitude
        const float wEdge = static_cast<float>(swLon);        // western  edge longitude
        centerRow = static_cast<int>((nEdge - cropCenterLat) * (samples - 1) + 0.5f);
        centerCol = static_cast<int>((cropCenterLon - wEdge)  * (samples - 1) + 0.5f);
    } else {
        centerRow = samples / 2;
        centerCol = samples / 2;
    }

    // Clamp crop to tile bounds.
    int r0 = centerRow - cropSize / 2;
    int c0 = centerCol - cropSize / 2;
    r0 = std::max(0, std::min(r0, samples - cropSize));
    c0 = std::max(0, std::min(c0, samples - cropSize));

    // Build TerrainModel.  HGT rows go N→S; TerrainModel rows go S→N (row 0 = south).
    TerrainModel t;
    t.width    = cropSize;
    t.height   = cropSize;
    t.cellSize = metersPerCell;
    t.heights.resize(cropSize * cropSize);

    for (int hgtRow = 0; hgtRow < cropSize; ++hgtRow) {
        const int terrainRow = cropSize - 1 - hgtRow;   // flip N↔S
        for (int col = 0; col < cropSize; ++col) {
            const int16_t v = raw[(r0 + hgtRow) * samples + (c0 + col)];
            t.heights[terrainRow * cropSize + col] = (v == -32768) ? 0.0f
                                                                   : static_cast<float>(v);
        }
    }

    return t;
}

// ---------------------------------------------------------------------------

TerrainModel TerrainLoader::makeFlat(int w, int h, float cell, float elev) {
    TerrainModel t;
    t.width = w; t.height = h; t.cellSize = cell;
    t.heights.assign(w * h, elev);
    return t;
}

TerrainModel TerrainLoader::makeHills(int w, int h, float cell, float amp) {
    TerrainModel t;
    t.width = w; t.height = h; t.cellSize = cell;
    t.heights.resize(w * h);
    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w; ++x) {
            float nx = static_cast<float>(x) / w;
            float nz = static_cast<float>(z) / h;
            t.heights[z * w + x] =
                amp * (0.5f * std::sin(nx * 6.28f * 2.0f) * std::cos(nz * 6.28f * 1.5f)
                     + 0.3f * std::sin(nx * 6.28f * 5.0f + 1.0f)
                     + 0.2f * std::cos(nz * 6.28f * 4.0f + 0.5f));
        }
    }
    return t;
}
