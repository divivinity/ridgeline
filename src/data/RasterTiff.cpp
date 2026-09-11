#include "RasterTiff.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

namespace {

constexpr std::uint16_t kTagImageWidth      = 256;
constexpr std::uint16_t kTagImageLength     = 257;
constexpr std::uint16_t kTagBitsPerSample   = 258;
constexpr std::uint16_t kTagCompression     = 259;
constexpr std::uint16_t kTagStripOffsets    = 273;
constexpr std::uint16_t kTagSamplesPerPixel = 277;
constexpr std::uint16_t kTagRowsPerStrip    = 278;
constexpr std::uint16_t kTagStripByteCounts = 279;
constexpr std::uint16_t kTagPlanarConfig    = 284;
constexpr std::uint16_t kTagPredictor       = 317;
constexpr std::uint16_t kTagTileWidth       = 322;
constexpr std::uint16_t kTagTileLength      = 323;
constexpr std::uint16_t kTagTileOffsets     = 324;
constexpr std::uint16_t kTagTileByteCounts  = 325;
constexpr std::uint16_t kTagSampleFormat    = 339;
constexpr std::uint16_t kTagModelPixelScale = 33550;
constexpr std::uint16_t kTagModelTiepoint   = 33922;

// Guards against a malformed header claiming a raster larger than any grid
// Ridgeline can hold, before any allocation is attempted.
constexpr std::int64_t kMaxPixels = 64LL * 1024 * 1024;

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("TIFF: " + what);
}

struct Reader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    bool littleEndian = true;

    void require(std::size_t offset, std::size_t length, const char* what) const {
        if (offset > size || length > size - offset)
            fail(std::string(what) + " runs past the end of the file");
    }

    std::uint16_t u16(std::size_t offset) const {
        require(offset, 2, "a 16-bit field");
        const std::uint8_t a = data[offset], b = data[offset + 1];
        return littleEndian ? static_cast<std::uint16_t>(a | (b << 8))
                            : static_cast<std::uint16_t>(b | (a << 8));
    }

    std::uint32_t u32(std::size_t offset) const {
        require(offset, 4, "a 32-bit field");
        const std::uint32_t a = data[offset], b = data[offset + 1];
        const std::uint32_t c = data[offset + 2], d = data[offset + 3];
        return littleEndian ? (a | (b << 8) | (c << 16) | (d << 24))
                            : (d | (c << 8) | (b << 16) | (a << 24));
    }

    double f64(std::size_t offset) const {
        require(offset, 8, "a 64-bit float");
        std::uint8_t raw[8];
        for (int i = 0; i < 8; ++i)
            raw[i] = data[offset + (littleEndian ? i : 7 - i)];
        double value = 0.0;
        std::memcpy(&value, raw, 8);
        return value;
    }

    float f32(std::size_t offset) const {
        require(offset, 4, "a 32-bit float");
        std::uint8_t raw[4];
        for (int i = 0; i < 4; ++i)
            raw[i] = data[offset + (littleEndian ? i : 3 - i)];
        float value = 0.0f;
        std::memcpy(&value, raw, 4);
        return value;
    }
};

struct Entry {
    std::uint16_t type = 0;
    std::uint32_t count = 0;
    std::size_t valueOffset = 0;   // where the values actually live
};

std::size_t typeSize(std::uint16_t type) {
    switch (type) {
        case 1: case 2: case 6: case 7: return 1;   // BYTE ASCII SBYTE UNDEFINED
        case 3: case 8:                 return 2;   // SHORT SSHORT
        case 4: case 9: case 11:        return 4;   // LONG SLONG FLOAT
        case 5: case 10: case 12:       return 8;   // RATIONAL SRATIONAL DOUBLE
        default:                        return 0;
    }
}

// Integer-valued tag entries. Rationals and doubles are read separately.
std::uint32_t entryValue(const Reader& r, const Entry& e, std::uint32_t index) {
    const std::size_t at = e.valueOffset + index * typeSize(e.type);
    switch (e.type) {
        case 1: case 6: case 7:
            r.require(at, 1, "a byte tag value");
            return r.data[at];
        case 3: case 8:  return r.u16(at);
        case 4: case 9:  return r.u32(at);
        default: fail("a tag holds a type this reader does not read as an integer");
    }
}

double entryDouble(const Reader& r, const Entry& e, std::uint32_t index) {
    const std::size_t at = e.valueOffset + index * typeSize(e.type);
    if (e.type == 12) return r.f64(at);
    if (e.type == 11) return static_cast<double>(r.f32(at));
    if (e.type == 5 || e.type == 10) {
        const double num = static_cast<double>(r.u32(at));
        const double den = static_cast<double>(r.u32(at + 4));
        return den != 0.0 ? num / den : 0.0;
    }
    return static_cast<double>(entryValue(r, e, index));
}

double sampleAt(const Reader& r, std::size_t offset, int bits,
                RasterTiff::Sample format) {
    switch (format) {
        case RasterTiff::Sample::Float:
            if (bits == 32) return static_cast<double>(r.f32(offset));
            if (bits == 64) return r.f64(offset);
            fail("a floating-point sample that is neither 32- nor 64-bit");
        case RasterTiff::Sample::SignedInt:
            if (bits == 8) {
                r.require(offset, 1, "a sample");
                return static_cast<double>(static_cast<std::int8_t>(r.data[offset]));
            }
            if (bits == 16)
                return static_cast<double>(static_cast<std::int16_t>(r.u16(offset)));
            if (bits == 32)
                return static_cast<double>(static_cast<std::int32_t>(r.u32(offset)));
            fail("a signed sample that is not 8, 16 or 32 bits");
        case RasterTiff::Sample::UnsignedInt:
            if (bits == 8) {
                r.require(offset, 1, "a sample");
                return static_cast<double>(r.data[offset]);
            }
            if (bits == 16) return static_cast<double>(r.u16(offset));
            if (bits == 32) return static_cast<double>(r.u32(offset));
            fail("an unsigned sample that is not 8, 16 or 32 bits");
    }
    fail("an unrecognised sample format");
}

} // namespace

double RasterTiff::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0.0;
    const std::size_t index = static_cast<std::size_t>(y) * width + x;
    return index < values.size() ? values[index] : 0.0;
}

RasterTiff RasterTiff::decode(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 8) fail("the file is too short to hold a header");

    Reader r;
    r.data = bytes.data();
    r.size = bytes.size();
    if (bytes[0] == 'I' && bytes[1] == 'I')      r.littleEndian = true;
    else if (bytes[0] == 'M' && bytes[1] == 'M') r.littleEndian = false;
    else fail("the byte-order mark is neither II nor MM");

    const std::uint16_t magic = r.u16(2);
    if (magic == 43)
        fail("this is a BigTIFF, which this reader does not handle");
    if (magic != 42)
        fail("the version field is " + std::to_string(magic) + ", not 42");

    const std::size_t ifdOffset = r.u32(4);
    const std::uint16_t entryCount = r.u16(ifdOffset);

    std::map<std::uint16_t, Entry> entries;
    for (std::uint16_t i = 0; i < entryCount; ++i) {
        const std::size_t at = ifdOffset + 2 + static_cast<std::size_t>(i) * 12;
        const std::uint16_t tag = r.u16(at);
        Entry e;
        e.type  = r.u16(at + 2);
        e.count = r.u32(at + 4);
        const std::size_t width = typeSize(e.type);
        if (width == 0) continue;                       // unknown type: skip it
        const std::size_t total = width * e.count;
        e.valueOffset = total <= 4 ? at + 8 : r.u32(at + 8);
        r.require(e.valueOffset, total, "a tag value");
        entries[tag] = e;
    }

    auto has = [&](std::uint16_t tag) { return entries.count(tag) != 0; };
    auto scalar = [&](std::uint16_t tag, std::uint32_t fallback) -> std::uint32_t {
        const auto it = entries.find(tag);
        if (it == entries.end() || it->second.count == 0) return fallback;
        return entryValue(r, it->second, 0);
    };

    if (!has(kTagImageWidth) || !has(kTagImageLength))
        fail("the image dimensions are missing");

    RasterTiff out;
    const std::uint32_t w = scalar(kTagImageWidth, 0);
    const std::uint32_t h = scalar(kTagImageLength, 0);
    if (w == 0 || h == 0) fail("the image has a zero dimension");
    if (static_cast<std::int64_t>(w) * h > kMaxPixels)
        fail("the image is larger than this reader will allocate");
    out.width  = static_cast<int>(w);
    out.height = static_cast<int>(h);

    const std::uint32_t samplesPerPixel = scalar(kTagSamplesPerPixel, 1);
    if (samplesPerPixel != 1)
        fail("the image has " + std::to_string(samplesPerPixel) +
             " samples per pixel; this reader handles one");

    const std::uint32_t compression = scalar(kTagCompression, 1);
    if (compression != 1)
        fail("compression " + std::to_string(compression) +
             " is not supported; request an uncompressed raster");

    const std::uint32_t predictor = scalar(kTagPredictor, 1);
    if (predictor != 1)
        fail("predictor " + std::to_string(predictor) + " is not supported");

    const std::uint32_t planar = scalar(kTagPlanarConfig, 1);
    if (planar != 1)
        fail("planar configuration " + std::to_string(planar) + " is not supported");

    out.bitsPerSample = static_cast<int>(scalar(kTagBitsPerSample, 8));
    if (out.bitsPerSample % 8 != 0)
        fail("a sample is " + std::to_string(out.bitsPerSample) +
             " bits; only whole bytes are supported");
    const std::size_t sampleBytes = static_cast<std::size_t>(out.bitsPerSample) / 8;

    switch (scalar(kTagSampleFormat, 1)) {
        case 1: out.sampleFormat = Sample::UnsignedInt; break;
        case 2: out.sampleFormat = Sample::SignedInt;   break;
        case 3: out.sampleFormat = Sample::Float;       break;
        default: fail("an unsupported SampleFormat");
    }

    out.values.assign(static_cast<std::size_t>(w) * h, 0.0);

    if (has(kTagTileOffsets)) {
        const std::uint32_t tileW = scalar(kTagTileWidth, 0);
        const std::uint32_t tileH = scalar(kTagTileLength, 0);
        if (tileW == 0 || tileH == 0) fail("a tiled image with no tile size");
        const Entry& offsets = entries.at(kTagTileOffsets);
        const std::uint32_t across = (w + tileW - 1) / tileW;
        const std::uint32_t down   = (h + tileH - 1) / tileH;
        if (offsets.count < static_cast<std::uint64_t>(across) * down)
            fail("fewer tile offsets than the image needs");
        for (std::uint32_t t = 0; t < across * down; ++t) {
            const std::size_t base = entryValue(r, offsets, t);
            r.require(base, static_cast<std::size_t>(tileW) * tileH * sampleBytes,
                      "a tile");
            const std::uint32_t originX = (t % across) * tileW;
            const std::uint32_t originY = (t / across) * tileH;
            for (std::uint32_t row = 0; row < tileH; ++row) {
                const std::uint32_t y = originY + row;
                if (y >= h) break;
                for (std::uint32_t col = 0; col < tileW; ++col) {
                    const std::uint32_t x = originX + col;
                    if (x >= w) continue;
                    const std::size_t at = base +
                        (static_cast<std::size_t>(row) * tileW + col) * sampleBytes;
                    out.values[static_cast<std::size_t>(y) * w + x] =
                        sampleAt(r, at, out.bitsPerSample, out.sampleFormat);
                }
            }
        }
    } else if (has(kTagStripOffsets)) {
        const std::uint32_t rowsPerStrip = scalar(kTagRowsPerStrip, h);
        if (rowsPerStrip == 0) fail("a stripped image with zero rows per strip");
        const Entry& offsets = entries.at(kTagStripOffsets);
        const std::uint32_t strips = (h + rowsPerStrip - 1) / rowsPerStrip;
        if (offsets.count < strips) fail("fewer strip offsets than the image needs");
        for (std::uint32_t s = 0; s < strips; ++s) {
            const std::size_t base = entryValue(r, offsets, s);
            const std::uint32_t firstRow = s * rowsPerStrip;
            const std::uint32_t rows = std::min(rowsPerStrip, h - firstRow);
            r.require(base, static_cast<std::size_t>(rows) * w * sampleBytes, "a strip");
            for (std::uint32_t row = 0; row < rows; ++row) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const std::size_t at = base +
                        (static_cast<std::size_t>(row) * w + x) * sampleBytes;
                    out.values[static_cast<std::size_t>(firstRow + row) * w + x] =
                        sampleAt(r, at, out.bitsPerSample, out.sampleFormat);
                }
            }
        }
    } else {
        fail("the image has neither strip nor tile offsets");
    }

    // Placement is optional: a raster whose bounds the caller already knows is
    // still usable. Only the north-up, axis-aligned single-tiepoint form is
    // read, because that is what a geographic-projection export produces and
    // silently mishandling a rotated raster would put canopy in the wrong place.
    if (has(kTagModelPixelScale) && has(kTagModelTiepoint)) {
        const Entry& scale = entries.at(kTagModelPixelScale);
        const Entry& tie   = entries.at(kTagModelTiepoint);
        if (scale.count >= 2 && tie.count >= 6) {
            const double rasterX = entryDouble(r, tie, 0);
            const double rasterY = entryDouble(r, tie, 1);
            const double lon     = entryDouble(r, tie, 3);
            const double lat     = entryDouble(r, tie, 4);
            const double stepLon = entryDouble(r, scale, 0);
            const double stepLat = entryDouble(r, scale, 1);
            if (rasterX == 0.0 && rasterY == 0.0 &&
                std::isfinite(lon) && std::isfinite(lat) &&
                stepLon > 0.0 && stepLat > 0.0) {
                out.georeferenced = true;
                out.westLonDeg  = lon;
                out.northLatDeg = lat;
                out.lonStepDeg  = stepLon;
                out.latStepDeg  = stepLat;
            }
        }
    }

    return out;
}
