#include "TerrainLoader.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <memory>
#ifdef HAVE_GDAL
#  include <gdal_priv.h>
#  include <ogr_spatialref.h>
#endif

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

    const int lonIntervalTenths = parseInt4(uhl + 20);  // tenths of arc-sec
    const int latIntervalTenths = parseInt4(uhl + 24);  // tenths of arc-sec
    const int numLon             = parseInt4(uhl + 51);  // columns
    const int numLat             = parseInt4(uhl + 55);  // rows per column

    if (numLon < 2 || numLat < 2 || lonIntervalTenths <= 0 || latIntervalTenths <= 0)
        throw std::runtime_error("DTED UHL contains invalid grid dimensions");

    // Latitude of SW corner — used to compute cell size in metres.
    const int originLonSec = parseOriginArcSec(uhl + 4, 8);
    const int originLatSec = parseOriginArcSec(uhl + 12, 8);
    const double centerLatDeg =
        originLatSec / 3600.0 + (numLat * latIntervalTenths) / (2.0 * 36000.0);

    const double cellLatDeg = latIntervalTenths / 36000.0;
    const double cellLonDeg = lonIntervalTenths / 36000.0;
    const float  cellSizeZM =
        static_cast<float>(TerrainModel::metersPerDegreeLat(centerLatDeg) * cellLatDeg);
    const float  cellSizeXM =
        static_cast<float>(TerrainModel::metersPerDegreeLon(centerLatDeg) * cellLonDeg);

    // Skip DSI (648) + ACC (2700); data starts at byte 3428.
    f.seekg(80 + 648 + 2700, std::ios::beg);

    TerrainModel t;
    t.width    = numLon;
    t.height   = numLat;
    t.setCellSpacing(cellSizeXM, cellSizeZM);
    t.setGeoReference(originLatSec / 3600.0, originLonSec / 3600.0,
                      cellLatDeg, cellLonDeg);
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
    if (fileSize == 25934402LL) {
        samples = 3601;
    } else if (fileSize == 2884802LL) {
        samples = 1201;
    } else {
        throw std::runtime_error(
            "HGT file has unexpected size (expected SRTM1 or SRTM3): " + path);
    }
    cropSize = std::clamp(cropSize, 2, samples);

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
        if (cropCenterLat < static_cast<float>(swLat) ||
            cropCenterLat > static_cast<float>(swLat + 1) ||
            cropCenterLon < static_cast<float>(swLon) ||
            cropCenterLon > static_cast<float>(swLon + 1)) {
            cropCenterLat = static_cast<float>(swLat) + 0.5f;
            cropCenterLon = static_cast<float>(swLon) + 0.5f;
        }
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
    const double sampleDeg = 1.0 / static_cast<double>(samples - 1);
    const double cropSouthLat =
        static_cast<double>(swLat + 1) -
        static_cast<double>(r0 + cropSize - 1) * sampleDeg;
    const double cropWestLon =
        static_cast<double>(swLon) + static_cast<double>(c0) * sampleDeg;
    const double cropCenterLatDeg =
        cropSouthLat + static_cast<double>(cropSize - 1) * sampleDeg * 0.5;
    t.setCellSpacing(
        static_cast<float>(TerrainModel::metersPerDegreeLon(cropCenterLatDeg) * sampleDeg),
        static_cast<float>(TerrainModel::metersPerDegreeLat(cropCenterLatDeg) * sampleDeg));
    t.setGeoReference(cropSouthLat, cropWestLon, sampleDeg, sampleDeg);
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

#ifndef HAVE_GDAL
TerrainModel TerrainLoader::fromGeoTIFF(const std::string& path,
                                        int maxSamples) {
    (void)maxSamples;
    throw std::runtime_error("GeoTIFF loading requires GDAL (not compiled in): " + path);
}
#else  // HAVE_GDAL

static bool rasterPointToWgs84(const OGRSpatialReference* srs,
                               const double gt[6],
                               double pixelCol,
                               double pixelRow,
                               double& lon,
                               double& lat) {
    double x = gt[0] + (pixelCol + 0.5) * gt[1] + (pixelRow + 0.5) * gt[2];
    double y = gt[3] + (pixelCol + 0.5) * gt[4] + (pixelRow + 0.5) * gt[5];

    if (srs) {
        OGRSpatialReference src(*srs);
        OGRSpatialReference dst;
        src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        dst.SetWellKnownGeogCS("WGS84");
        dst.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

        OGRCoordinateTransformation* tx =
            OGRCreateCoordinateTransformation(&src, &dst);
        if (!tx) return false;
        const bool ok = tx->Transform(1, &x, &y);
        OCTDestroyCoordinateTransformation(tx);
        if (!ok) return false;
    } else {
        if (x < -180.0 || x > 180.0 || y < -90.0 || y > 90.0)
            return false;
    }

    lon = x;
    lat = y;
    return true;
}

TerrainModel TerrainLoader::fromGeoTIFF(const std::string& path,
                                        int maxSamples) {
    GDALAllRegister();
    std::unique_ptr<GDALDataset, decltype(&GDALClose)> ds(
        static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly)),
        GDALClose);
    if (!ds) throw std::runtime_error("Cannot open GeoTIFF/DEM file: " + path);

    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band) throw std::runtime_error("DEM contains no raster band: " + path);

    const int srcW = ds->GetRasterXSize();
    const int srcH = ds->GetRasterYSize();
    if (srcW < 2 || srcH < 2)
        throw std::runtime_error("DEM raster is too small: " + path);

    maxSamples = std::max(maxSamples, 2);
    const int stride = std::max(1, static_cast<int>(
        std::ceil(static_cast<double>(std::max(srcW, srcH)) / maxSamples)));
    const int outW = std::max(2, (srcW + stride - 1) / stride);
    const int outH = std::max(2, (srcH + stride - 1) / stride);

    std::vector<float> raw(outW * outH);
    CPLErr err = band->RasterIO(GF_Read, 0, 0, srcW, srcH,
                                raw.data(), outW, outH,
                                GDT_Float32, 0, 0);
    if (err != CE_None)
        throw std::runtime_error("GDAL failed to read DEM raster: " + path);

    double gt[6] = {};
    if (ds->GetGeoTransform(gt) != CE_None)
        throw std::runtime_error("DEM is missing a geotransform: " + path);

    const double scaleX = static_cast<double>(srcW - 1) / static_cast<double>(outW - 1);
    const double scaleY = static_cast<double>(srcH - 1) / static_cast<double>(outH - 1);
    const double projectedCellX =
        std::hypot(gt[1], gt[4]) * scaleX;
    const double projectedCellZ =
        std::hypot(gt[2], gt[5]) * scaleY;

    double westLon = 0.0, eastLon = 0.0, southLat = 0.0, northLat = 0.0;
    const OGRSpatialReference* srs = ds->GetSpatialRef();
    const bool haveWgs84 =
        rasterPointToWgs84(srs, gt, 0.0, srcH - 1.0, westLon, southLat) &&
        rasterPointToWgs84(srs, gt, srcW - 1.0, 0.0, eastLon, northLat);

    TerrainModel t;
    t.width = outW;
    t.height = outH;

    if (haveWgs84) {
        const double cellLon = (eastLon - westLon) / static_cast<double>(outW - 1);
        const double cellLat = (northLat - southLat) / static_cast<double>(outH - 1);
        const double centerLat = (northLat + southLat) * 0.5;
        t.setCellSpacing(
            static_cast<float>(TerrainModel::metersPerDegreeLon(centerLat) * std::abs(cellLon)),
            static_cast<float>(TerrainModel::metersPerDegreeLat(centerLat) * std::abs(cellLat)));
        t.setGeoReference(southLat, westLon, std::abs(cellLat), cellLon);
    } else {
        t.setCellSpacing(static_cast<float>(projectedCellX),
                         static_cast<float>(projectedCellZ));
    }

    int hasNoData = 0;
    const double noData = band->GetNoDataValue(&hasNoData);
    t.heights.resize(outW * outH);
    for (int srcRow = 0; srcRow < outH; ++srcRow) {
        const int terrainRow = outH - 1 - srcRow;
        for (int col = 0; col < outW; ++col) {
            float v = raw[srcRow * outW + col];
            if (!std::isfinite(v) || (hasNoData && std::abs(v - noData) < 0.001))
                v = 0.0f;
            t.heights[terrainRow * outW + col] = v;
        }
    }

    return t;
}
#endif // HAVE_GDAL / !HAVE_GDAL

// ---------------------------------------------------------------------------

TerrainModel TerrainLoader::makeFlat(int w, int h, float cell, float elev) {
    TerrainModel t;
    t.width = w; t.height = h; t.setCellSpacing(cell, cell);
    t.heights.assign(w * h, elev);
    return t;
}

TerrainModel TerrainLoader::makeHills(int w, int h, float cell, float amp) {
    TerrainModel t;
    t.width = w; t.height = h; t.setCellSpacing(cell, cell);
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
