#include "CoverageGeoTiff.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace {

constexpr std::uint16_t kShort = 3;
constexpr std::uint16_t kLong = 4;
constexpr std::uint16_t kAscii = 2;
constexpr std::uint16_t kDouble = 12;

// GDAL's private tags, which QGIS and ArcGIS also read: band metadata and the
// no-data value.
constexpr std::uint16_t kTagGdalMetadata = 42112;
constexpr std::uint16_t kTagGdalNoData = 42113;

struct Tag {
    std::uint16_t id;
    std::uint16_t type;
    std::uint32_t count;
    std::vector<std::uint8_t> value; // little-endian, already serialised
};

void putU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void putF64(std::vector<std::uint8_t>& out, double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
}

Tag shortTag(std::uint16_t id, std::vector<std::uint16_t> values) {
    Tag tag{id, kShort, static_cast<std::uint32_t>(values.size()), {}};
    for (const auto v : values) putU16(tag.value, v);
    return tag;
}

Tag longTag(std::uint16_t id, std::uint32_t value) {
    Tag tag{id, kLong, 1, {}};
    putU32(tag.value, value);
    return tag;
}

Tag doubleTag(std::uint16_t id, std::vector<double> values) {
    Tag tag{id, kDouble, static_cast<std::uint32_t>(values.size()), {}};
    for (const auto v : values) putF64(tag.value, v);
    return tag;
}

Tag asciiTag(std::uint16_t id, const std::string& text) {
    Tag tag{id, kAscii, static_cast<std::uint32_t>(text.size() + 1), {}};
    tag.value.assign(text.begin(), text.end());
    tag.value.push_back(0);
    return tag;
}

} // namespace

namespace CoverageGeoTiff {

bool canEncode(const FieldGrid& grid, const TerrainModel& terrain) {
    return grid.valid() && terrain.hasGeoReference() &&
           terrain.metersPerCellX() > 0.0f && terrain.metersPerCellZ() > 0.0f &&
           terrain.cellLatDeg > 0.0 && terrain.cellLonDeg > 0.0 &&
           grid.cellCount() <= std::numeric_limits<std::uint32_t>::max() / 4;
}

std::vector<std::uint8_t> encode(const FieldGrid& grid, const TerrainModel& terrain) {
    if (!canEncode(grid, terrain)) return {};

    // World metres map linearly onto degrees, so a cell's edges in world
    // space give its edges on the ground. Cell i is centred on
    // originXZ + i * cellSize, so its west edge is half a cell before that.
    const double cell = grid.cellSize;
    const double lonPerM = terrain.cellLonDeg / terrain.metersPerCellX();
    const double latPerM = terrain.cellLatDeg / terrain.metersPerCellZ();
    const double westWorldX = grid.originXZ.x - cell * 0.5;
    const double southWorldZ = grid.originXZ.y - cell * 0.5;
    const double westLon = terrain.originLonDeg + westWorldX * lonPerM;
    const double northLat = terrain.originLatDeg +
        (southWorldZ + cell * grid.height) * latPerM;

    const std::uint32_t width = static_cast<std::uint32_t>(grid.width);
    const std::uint32_t height = static_cast<std::uint32_t>(grid.height);
    const std::uint32_t pixelBytes = width * height * 4;

    // Header, then pixels, then the directory and the values too large to sit
    // inside it.
    std::vector<std::uint8_t> out;
    out.reserve(8 + pixelBytes + 1024);
    out.push_back('I');
    out.push_back('I');
    putU16(out, 42);
    putU32(out, 0); // directory offset, patched below

    // TIFF rows run north to south; grid rows run south to north.
    const float noData = std::numeric_limits<float>::quiet_NaN();
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::size_t gz = height - 1 - row;
        for (std::uint32_t x = 0; x < width; ++x) {
            const float v = grid.dbm[gz * width + x];
            const float value = std::isfinite(v) ? v : noData;
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof bits);
            putU32(out, bits);
        }
    }

    // Geographic WGS 84, degrees, each pixel an area.
    const std::vector<std::uint16_t> geoKeys{
        1, 1, 0, 4,          // directory version 1.1.0, four keys
        1024, 0, 1, 2,       // GTModelTypeGeoKey = ModelTypeGeographic
        1025, 0, 1, 1,       // GTRasterTypeGeoKey = RasterPixelIsArea
        2048, 0, 1, 4326,    // GeographicTypeGeoKey = WGS 84
        2054, 0, 1, 9102,    // GeogAngularUnitsGeoKey = degree
    };

    // Ascending tag order is required.
    std::vector<Tag> tags{
        longTag(256, width),                           // ImageWidth
        longTag(257, height),                          // ImageLength
        shortTag(258, {32}),                           // BitsPerSample
        shortTag(259, {1}),                            // Compression: none
        shortTag(262, {1}),                            // Photometric: BlackIsZero
        longTag(273, 8),                               // StripOffsets
        shortTag(277, {1}),                            // SamplesPerPixel
        longTag(278, height),                          // RowsPerStrip
        longTag(279, pixelBytes),                      // StripByteCounts
        shortTag(284, {1}),                            // PlanarConfiguration
        asciiTag(305, "Ridgeline"),                    // Software
        shortTag(339, {3}),                            // SampleFormat: IEEE float
        doubleTag(33550, {cell * lonPerM, cell * latPerM, 0.0}),    // ModelPixelScale
        doubleTag(33922, {0.0, 0.0, 0.0, westLon, northLat, 0.0}),  // ModelTiepoint
        shortTag(34735, geoKeys),                      // GeoKeyDirectory
        asciiTag(kTagGdalMetadata,
                 "<GDALMetadata>"
                 "<Item name=\"DESCRIPTION\" sample=\"0\" role=\"description\">"
                 "Desired signal</Item>"
                 "<Item name=\"UNITTYPE\" sample=\"0\" role=\"unittype\">dBm</Item>"
                 "</GDALMetadata>"),
        asciiTag(kTagGdalNoData, "nan"),
    };

    if (out.size() % 2) out.push_back(0);
    const std::uint32_t directory = static_cast<std::uint32_t>(out.size());
    for (int i = 0; i < 4; ++i)
        out[4 + i] = static_cast<std::uint8_t>(directory >> (8 * i));

    const std::size_t directoryBytes = 2 + tags.size() * 12 + 4;
    std::uint32_t overflow = directory + static_cast<std::uint32_t>(directoryBytes);
    std::vector<std::uint8_t> extra;
    putU16(out, static_cast<std::uint16_t>(tags.size()));
    for (const Tag& tag : tags) {
        putU16(out, tag.id);
        putU16(out, tag.type);
        putU32(out, tag.count);
        if (tag.value.size() <= 4) {
            std::vector<std::uint8_t> inline_ = tag.value;
            inline_.resize(4, 0);
            out.insert(out.end(), inline_.begin(), inline_.end());
        } else {
            putU32(out, overflow);
            extra.insert(extra.end(), tag.value.begin(), tag.value.end());
            if (extra.size() % 2) extra.push_back(0);
            overflow = directory + static_cast<std::uint32_t>(directoryBytes + extra.size());
        }
    }
    putU32(out, 0); // no further directories
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

} // namespace CoverageGeoTiff
