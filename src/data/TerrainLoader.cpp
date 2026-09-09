#include "TerrainLoader.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>
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
//  12-19  Origin latitude   DDDMMSSH  (e.g. "0450000N")
//  20-23  Longitude interval, tenths of arc-seconds ("0010" = 1 arc-sec)
//  24-27  Latitude  interval, tenths of arc-seconds
//  28-31  Absolute vertical accuracy
//  32-34  Security code (3 chars)
//  35-46  Unique reference (12 chars)
//  47-50  Number of longitude lines   (columns, west→east)
//  51-54  Number of latitude  points  (rows, south→north)
//  55     Multiple accuracy flag
//  56-79  Reserved
// Then: DSI 648 bytes, ACC 2700 bytes  → data starts at byte 3428.
//
// Data records (one per longitude column):
//   byte 0    : 0xAA sentinel
//   bytes 1-3 : block count (24-bit big-endian)
//   bytes 4-5 : longitude count (16-bit big-endian, 0-based column index)
//   bytes 6-7 : latitude  count (16-bit big-endian, always 0)
//   bytes 8.. : numLat × signed-magnitude big-endian elevations (south→north)
//   last 4    : unsigned byte-sum checksum, big-endian
// Void elevation = 0xFFFF (-32767). Reject rather than invent a height.
// Standard UHL-first, complete sequential profiles only; no variant repair.

static int parseDigits(const char* p, int count) {
    int value = 0;
    for (int i = 0; i < count; ++i) {
        if (p[i] < '0' || p[i] > '9')
            throw std::runtime_error("DTED UHL: invalid numeric field");
        value = value * 10 + p[i] - '0';
    }
    return value;
}

// Parse standard DDDMMSSH into total arc-seconds (W/S negative).
static int parseOriginArcSec(const char* p, bool latitude) {
    const char hemi = p[7];
    if (latitude ? (hemi != 'N' && hemi != 'S') : (hemi != 'E' && hemi != 'W'))
        throw std::runtime_error("DTED UHL: cannot parse origin coordinate");

    const int deg = parseDigits(p, 3);
    const int min = parseDigits(p + 3, 2);
    const int sec = parseDigits(p + 5, 2);
    const int limit = latitude ? 90 : 180;
    if (min >= 60 || sec >= 60 || deg > limit || (deg == limit && (min || sec)))
        throw std::runtime_error("DTED UHL: origin outside coordinate bounds");

    int total = deg * 3600 + min * 60 + sec;
    return (hemi == 'W' || hemi == 'S') ? -total : total;
}

static uint16_t readBE16(const char* p) {
    return static_cast<uint16_t>(
        (static_cast<uint8_t>(p[0]) << 8) | static_cast<uint8_t>(p[1]));
}

TerrainModel TerrainLoader::fromDTED(const std::string& path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open DTED file: " + path);

    // --- UHL ---
    char uhl[80];
    f.read(uhl, 80);
    if (!f || std::strncmp(uhl, "UHL1", 4) != 0)
        throw std::runtime_error("Not a valid DTED file (missing UHL1): " + path);

    const int lonIntervalTenths = parseDigits(uhl + 20, 4);
    const int latIntervalTenths = parseDigits(uhl + 24, 4);
    const int numLon = parseDigits(uhl + 47, 4);
    const int numLat = parseDigits(uhl + 51, 4);

    if (numLon < 2 || numLat < 2 || lonIntervalTenths <= 0 || latIntervalTenths <= 0)
        throw std::runtime_error("DTED UHL contains invalid grid dimensions");

    // Latitude of SW corner — used to compute cell size in metres.
    const int originLonSec = parseOriginArcSec(uhl + 4, false);
    const int originLatSec = parseOriginArcSec(uhl + 12, true);
    const double centerLatDeg =
        originLatSec / 3600.0 + ((numLat - 1) * latIntervalTenths) / (2.0 * 36000.0);

    const double cellLatDeg = latIntervalTenths / 36000.0;
    const double cellLonDeg = lonIntervalTenths / 36000.0;
    if (originLatSec / 3600.0 + (numLat - 1) * cellLatDeg > 90.0 ||
        originLonSec / 3600.0 + (numLon - 1) * cellLonDeg > 180.0)
        throw std::runtime_error("DTED UHL: grid extent outside coordinate bounds");
    const float  cellSizeZM =
        static_cast<float>(TerrainModel::metersPerDegreeLat(centerLatDeg) * cellLatDeg);
    const float  cellSizeXM =
        static_cast<float>(TerrainModel::metersPerDegreeLon(centerLatDeg) * cellLonDeg);

    char metadata[648 + 2700];
    f.read(metadata, sizeof(metadata));
    if (!f || std::memcmp(metadata, "DSI", 3) || std::memcmp(metadata + 648, "ACC", 3))
        throw std::runtime_error("DTED: missing DSI or ACC record");
    // DSI bytes 145-149 (one-based) declare the horizontal datum. This
    // decoder does not reproject, so never label another/unknown CRS WGS84.
    if (std::memcmp(metadata + 144, "WGS84", 5) != 0)
        throw std::runtime_error("DTED: unsupported or missing horizontal datum; WGS84 is required");
    const int recordSize = 8 + numLat * 2 + 4;
    const std::streamoff requiredSize = 3428 + static_cast<std::streamoff>(numLon) * recordSize;
    f.seekg(0, std::ios::end);
    if (!f || f.tellg() < requiredSize)
        throw std::runtime_error("DTED: truncated elevation records");
    f.seekg(3428, std::ios::beg);

    TerrainModel t;
    t.width    = numLon;
    t.height   = numLat;
    t.setCellSpacing(cellSizeXM, cellSizeZM);
    t.setGeoReference(originLatSec / 3600.0, originLonSec / 3600.0,
                      cellLatDeg, cellLonDeg);
    t.heights.assign(numLon * numLat, 0.0f);

    std::vector<char> rec(recordSize);

    for (int col = 0; col < numLon; ++col) {
        f.read(rec.data(), recordSize);
        if (!f)
            throw std::runtime_error(
                "Unexpected end of DTED file at column " + std::to_string(col));

        if (static_cast<uint8_t>(rec[0]) != 0xAA)
            throw std::runtime_error(
                "Invalid DTED data record sentinel at column " + std::to_string(col));

        if (readBE16(rec.data() + 4) != col || readBE16(rec.data() + 6) != 0)
            throw std::runtime_error("DTED: invalid profile index at column " + std::to_string(col));
        uint32_t sum = 0, storedSum = 0;
        for (int i = 0; i < recordSize - 4; ++i) sum += static_cast<uint8_t>(rec[i]);
        for (int i = recordSize - 4; i < recordSize; ++i)
            storedSum = (storedSum << 8) | static_cast<uint8_t>(rec[i]);
        if (sum != storedSum)
            throw std::runtime_error("DTED: checksum mismatch at column " + std::to_string(col));

        for (int row = 0; row < numLat; ++row) {
            const uint16_t raw = readBE16(rec.data() + 8 + row * 2);
            if (raw == 0xffff)
                throw std::runtime_error("DTED: missing elevation at column " + std::to_string(col)
                    + ", row " + std::to_string(row));
            const int magnitude = raw & 0x7fff;
            const float h = static_cast<float>((raw & 0x8000) ? -magnitude : magnitude);
            // DTED: col = west→east (x), row = south→north (z).
            t.heights[row * numLon + col] = h;
        }
    }

    t.validate();
    return t;
}

// ---------------------------------------------------------------------------
// SRTM HGT parser
// ---------------------------------------------------------------------------
// Format: raw signed big-endian int16, row-major, north→south.
// SRTM1: 3601×3601 samples (25 934 402 bytes), ~30.87 m/cell at 35°N.
// SRTM3: 1201×1201 samples ( 2 884 802 bytes), ~92.6  m/cell at 35°N.
// Void value: -32768. Reject missing samples within the selected analysis area.
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

struct HgtData {
    int samples = 0;
    std::vector<int16_t> values;
};

static HgtData readHGTData(const std::string& path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open HGT file: " + path);

    f.seekg(0, std::ios::end);
    const auto fileSize = static_cast<long long>(f.tellg());
    f.seekg(0, std::ios::beg);

    HgtData result;
    if (fileSize == 25934402LL) {
        result.samples = 3601;
    } else if (fileSize == 2884802LL) {
        result.samples = 1201;
    } else {
        throw std::runtime_error(
            "HGT file has unexpected size (expected SRTM1 or SRTM3): " + path);
    }

    const int total = result.samples * result.samples;
    result.values.resize(total);
    f.read(reinterpret_cast<char*>(result.values.data()),
           static_cast<std::streamsize>(total) * 2);
    if (!f)
        throw std::runtime_error("Failed to read HGT data from: " + path);

    for (auto& v : result.values) {
        const auto u = static_cast<uint16_t>(v);
        v = static_cast<int16_t>(((u & 0xFF00u) >> 8) | ((u & 0x00FFu) << 8));
    }
    return result;
}

TerrainModel TerrainLoader::fromHGT(const std::string& path,
                                    float cropCenterLat,
                                    float cropCenterLon,
                                    int   cropSize) {
    HgtData source = readHGTData(path);
    const int samples = source.samples;
    cropSize = std::clamp(cropSize, 2, samples);

    // SRTM HGT stores no coordinate metadata internally. Only a standard
    // [NS]dd[EW]ddd filename can establish an authoritative WGS84 origin.
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

    // Build TerrainModel. HGT rows go N→S; TerrainModel rows go S→N (row 0 = south).
    TerrainModel t;
    t.width    = cropSize;
    t.height   = cropSize;
    const double sampleDeg = 1.0 / static_cast<double>(samples - 1);

    // Without a standard SRTM filename, the file has no authoritative WGS84
    // origin. Keep useful physical cell spacing, but do not fabricate a map
    // location that would misalign imagery or exported coordinates.
    if (haveOrigin) {
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
    } else {
        const double spacingLat = std::clamp(
            std::isfinite(static_cast<double>(cropCenterLat))
                ? static_cast<double>(cropCenterLat) : 0.0,
            -89.0, 89.0);
        t.setCellSpacing(
            static_cast<float>(TerrainModel::metersPerDegreeLon(spacingLat) * sampleDeg),
            static_cast<float>(TerrainModel::metersPerDegreeLat(spacingLat) * sampleDeg));
    }
    t.heights.resize(cropSize * cropSize);

    for (int hgtRow = 0; hgtRow < cropSize; ++hgtRow) {
        const int terrainRow = cropSize - 1 - hgtRow;   // flip N↔S
        for (int col = 0; col < cropSize; ++col) {
            const int16_t v = source.values[(r0 + hgtRow) * samples + (c0 + col)];
            if (v == -32768)
                throw std::runtime_error(
                    "HGT contains missing elevation in the selected area. "
                    "Choose a smaller area or a void-filled elevation dataset: " + path);
            t.heights[terrainRow * cropSize + col] = static_cast<float>(v);
        }
    }

    return t;
}

std::string TerrainLoader::hgtTileName(int swLat, int swLon) {
    std::ostringstream out;
    out << (swLat >= 0 ? 'N' : 'S')
        << std::setw(2) << std::setfill('0') << std::abs(swLat)
        << (swLon >= 0 ? 'E' : 'W')
        << std::setw(3) << std::setfill('0') << std::abs(swLon)
        << ".hgt";
    return out.str();
}

std::vector<std::string> TerrainLoader::requiredHGTTiles(
        double centerLat, double centerLon, double extentKm) {
    if (!std::isfinite(centerLat) || !std::isfinite(centerLon) ||
        centerLat <= -90.0 || centerLat >= 90.0 ||
        centerLon < -180.0 || centerLon > 180.0 ||
        !std::isfinite(extentKm) || extentKm <= 0.0) {
        throw std::invalid_argument("Invalid location or terrain extent");
    }

    const double halfM = extentKm * 500.0;
    const double halfLatDeg = halfM / TerrainModel::metersPerDegreeLat(centerLat);
    const double halfLonDeg = halfM / TerrainModel::metersPerDegreeLon(centerLat);
    const double south = centerLat - halfLatDeg;
    const double north = centerLat + halfLatDeg;
    const double west  = centerLon - halfLonDeg;
    const double east  = centerLon + halfLonDeg;

    const int minLat = static_cast<int>(std::floor(south));
    const int maxLat = static_cast<int>(std::floor(std::nextafter(north, south)));
    const int minLon = static_cast<int>(std::floor(west));
    const int maxLon = static_cast<int>(std::floor(std::nextafter(east, west)));

    std::vector<std::string> names;
    for (int lat = minLat; lat <= maxLat; ++lat)
        for (int lon = minLon; lon <= maxLon; ++lon)
            names.push_back(hgtTileName(lat, lon));
    return names;
}

TerrainModel TerrainLoader::fromHGTDirectory(const std::string& directory,
                                              double centerLat,
                                              double centerLon,
                                              double extentKm,
                                              int maxSamplesPerAxis) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(fs::u8path(directory)))
        throw std::runtime_error("HGT terrain directory does not exist: " + directory);
    maxSamplesPerAxis = std::clamp(maxSamplesPerAxis, 64, 4096);

    const auto required = requiredHGTTiles(centerLat, centerLon, extentKm);
    std::vector<std::string> missing;
    for (const auto& name : required) {
        if (!fs::is_regular_file(fs::u8path(directory) / name)) missing.push_back(name);
    }
    if (!missing.empty()) {
        std::ostringstream msg;
        msg << "Terrain data is missing for the selected location. Required HGT tile";
        msg << (missing.size() == 1 ? ": " : "s: ");
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i) msg << ", ";
            msg << missing[i];
        }
        throw std::runtime_error(msg.str());
    }

    std::map<std::string, HgtData> tiles;
    auto loadTile = [&](int swLat, int swLon) -> const HgtData& {
        const std::string name = hgtTileName(swLat, swLon);
        auto it = tiles.find(name);
        if (it == tiles.end()) {
            HgtData data = readHGTData((fs::u8path(directory) / name).u8string());
            if (!tiles.empty() && data.samples != tiles.begin()->second.samples)
                throw std::runtime_error("HGT mosaic contains mixed tile resolutions");
            it = tiles.emplace(name, std::move(data)).first;
        }
        return it->second;
    };

    const int centerTileLat = static_cast<int>(std::floor(centerLat));
    const int centerTileLon = static_cast<int>(std::floor(centerLon));
    const int sourceSamples = loadTile(centerTileLat, centerTileLon).samples;
    const int intervals = sourceSamples - 1;
    const double sourceSampleDeg = 1.0 / static_cast<double>(intervals);
    const double extentM = extentKm * 1000.0;

    const int naturalW = std::max(2, static_cast<int>(std::round(
        extentM / (TerrainModel::metersPerDegreeLon(centerLat) * sourceSampleDeg))) + 1);
    const int naturalH = std::max(2, static_cast<int>(std::round(
        extentM / (TerrainModel::metersPerDegreeLat(centerLat) * sourceSampleDeg))) + 1);
    const int stride = std::max(1, static_cast<int>(std::ceil(
        static_cast<double>(std::max(naturalW, naturalH)) / maxSamplesPerAxis)));
    const int outW = std::max(2, (naturalW - 1) / stride + 1);
    const int outH = std::max(2, (naturalH - 1) / stride + 1);
    const double outSampleDeg = sourceSampleDeg * stride;
    const double southLat = centerLat - (outH - 1) * outSampleDeg * 0.5;
    const double westLon  = centerLon - (outW - 1) * outSampleDeg * 0.5;

    TerrainModel terrain;
    terrain.width = outW;
    terrain.height = outH;
    terrain.setCellSpacing(
        static_cast<float>(TerrainModel::metersPerDegreeLon(centerLat) * outSampleDeg),
        static_cast<float>(TerrainModel::metersPerDegreeLat(centerLat) * outSampleDeg));
    terrain.setGeoReference(southLat, westLon, outSampleDeg, outSampleDeg);
    terrain.heights.resize(outW * outH);

    for (int z = 0; z < outH; ++z) {
        const double lat = southLat + z * outSampleDeg;
        int swLat = static_cast<int>(std::floor(lat));
        if (std::abs(lat - std::round(lat)) < 1e-10)
            swLat = static_cast<int>(std::round(lat)) - 1;
        for (int x = 0; x < outW; ++x) {
            const double lon = westLon + x * outSampleDeg;
            int swLon = static_cast<int>(std::floor(lon));
            if (std::abs(lon - std::round(lon)) < 1e-10)
                swLon = static_cast<int>(std::round(lon)) - 1;

            const HgtData& tile = loadTile(swLat, swLon);
            const int row = std::clamp(static_cast<int>(std::llround(
                (static_cast<double>(swLat + 1) - lat) * intervals)),
                0, intervals);
            const int col = std::clamp(static_cast<int>(std::llround(
                (lon - static_cast<double>(swLon)) * intervals)),
                0, intervals);
            const int16_t raw = tile.values[row * tile.samples + col];
            if (raw == -32768)
                throw std::runtime_error(
                    "HGT contains missing elevation in the selected area. "
                    "Choose a smaller area or a void-filled elevation dataset: "
                    + hgtTileName(swLat, swLon));
            terrain.heights[z * outW + x] = static_cast<float>(raw);
        }
    }
    return terrain;
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

    // Ridgeline's terrain grid assumes X is east-west and Z is south-north.
    // A rotated/sheared affine transform cannot be represented faithfully by
    // the current TerrainModel, so reject it rather than silently misplacing RF
    // results and imagery. Such DEMs should be warped to a north-up raster first.
    constexpr double kAffineTolerance = 1e-12;
    if (std::abs(gt[2]) > kAffineTolerance || std::abs(gt[4]) > kAffineTolerance)
        throw std::runtime_error(
            "Rotated/sheared GeoTIFFs are not supported; warp the DEM to a north-up raster first: "
            + path);
    if (std::abs(gt[1]) <= kAffineTolerance || std::abs(gt[5]) <= kAffineTolerance)
        throw std::runtime_error("DEM geotransform has invalid pixel spacing: " + path);

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
        if (!(eastLon > westLon) || !(northLat > southLat))
            throw std::runtime_error(
                "GeoTIFF orientation cannot be represented by Ridgeline's north-up WGS84 grid: "
                + path);
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
