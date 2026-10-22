#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Synthetic TIFF byte writer shared by decoder and window integration tests.
// Internal linkage keeps helpers local to each test translation unit.
namespace {
struct TiffBuilder {
    bool littleEndian = true;
    int width = 0;
    int height = 0;
    int bitsPerSample = 16;
    int sampleFormat = 2;          // 1 unsigned, 2 signed, 3 float
    int compression = 1;
    int samplesPerPixel = 1;
    bool tiled = false;
    int tileSize = 0;              // square, when tiled
    int rowsPerStrip = 0;          // 0 means one strip
    std::uint16_t magic = 42;
    bool geo = false;
    double westLon = 0.0, northLat = 0.0, step = 0.0;
    std::vector<double> samples;   // width*height, row-major

    std::vector<std::uint8_t> build() const;
};

void put16(std::vector<std::uint8_t>& out, std::uint16_t v, bool le) {
    if (le) { out.push_back(v & 0xff); out.push_back((v >> 8) & 0xff); }
    else    { out.push_back((v >> 8) & 0xff); out.push_back(v & 0xff); }
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v, bool le) {
    for (int i = 0; i < 4; ++i)
        out.push_back((v >> (le ? 8 * i : 8 * (3 - i))) & 0xff);
}

void put32At(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t v, bool le) {
    for (int i = 0; i < 4; ++i)
        out[at + i] = (v >> (le ? 8 * i : 8 * (3 - i))) & 0xff;
}

void putDouble(std::vector<std::uint8_t>& out, double v, bool le) {
    std::uint8_t raw[8];
    std::memcpy(raw, &v, 8);
    for (int i = 0; i < 8; ++i) out.push_back(raw[le ? i : 7 - i]);
}

void putSample(std::vector<std::uint8_t>& out, double v, int bits, int format, bool le) {
    if (format == 3) {
        if (bits == 32) {
            const float f = static_cast<float>(v);
            std::uint8_t raw[4];
            std::memcpy(raw, &f, 4);
            for (int i = 0; i < 4; ++i) out.push_back(raw[le ? i : 3 - i]);
            return;
        }
        putDouble(out, v, le);
        return;
    }
    const long long asInt = std::llround(v);
    if (bits == 8)  { out.push_back(static_cast<std::uint8_t>(asInt & 0xff)); return; }
    if (bits == 16) { put16(out, static_cast<std::uint16_t>(asInt & 0xffff), le); return; }
    put32(out, static_cast<std::uint32_t>(asInt), le);
}

std::vector<std::uint8_t> TiffBuilder::build() const {
    const bool le = littleEndian;
    const int sampleBytes = bitsPerSample / 8;

    // Pixel payload first, laid out the way the layout flag says.
    std::vector<std::uint8_t> payload;
    std::vector<std::uint32_t> blockLengths;
    if (tiled) {
        const int across = (width + tileSize - 1) / tileSize;
        const int down   = (height + tileSize - 1) / tileSize;
        for (int ty = 0; ty < down; ++ty) {
            for (int tx = 0; tx < across; ++tx) {
                const std::size_t before = payload.size();
                for (int r = 0; r < tileSize; ++r) {
                    for (int c = 0; c < tileSize; ++c) {
                        const int x = tx * tileSize + c, y = ty * tileSize + r;
                        const double v = (x < width && y < height)
                            ? samples[static_cast<std::size_t>(y) * width + x] : 0.0;
                        putSample(payload, v, bitsPerSample, sampleFormat, le);
                    }
                }
                blockLengths.push_back(
                    static_cast<std::uint32_t>(payload.size() - before));
            }
        }
    } else {
        const int rows = rowsPerStrip > 0 ? rowsPerStrip : height;
        for (int first = 0; first < height; first += rows) {
            const std::size_t before = payload.size();
            for (int r = first; r < std::min(first + rows, height); ++r)
                for (int x = 0; x < width; ++x)
                    putSample(payload, samples[static_cast<std::size_t>(r) * width + x],
                              bitsPerSample, sampleFormat, le);
            blockLengths.push_back(
                static_cast<std::uint32_t>(payload.size() - before));
        }
    }

    struct Tag { std::uint16_t id; std::uint16_t type; std::vector<std::uint32_t> ints; };
    std::vector<Tag> tags;
    auto addShort = [&](std::uint16_t id, std::uint32_t v) {
        tags.push_back({id, 3, {v}});
    };
    auto addLong = [&](std::uint16_t id, std::vector<std::uint32_t> v) {
        tags.push_back({id, 4, std::move(v)});
    };

    addLong(256, {static_cast<std::uint32_t>(width)});
    addLong(257, {static_cast<std::uint32_t>(height)});
    addShort(258, static_cast<std::uint32_t>(bitsPerSample));
    addShort(259, static_cast<std::uint32_t>(compression));
    addShort(262, 1);
    addShort(277, static_cast<std::uint32_t>(samplesPerPixel));
    addShort(284, 1);
    addShort(339, static_cast<std::uint32_t>(sampleFormat));
    if (tiled) {
        addShort(322, static_cast<std::uint32_t>(tileSize));
        addShort(323, static_cast<std::uint32_t>(tileSize));
        addLong(324, std::vector<std::uint32_t>(blockLengths.size(), 0));   // patched
        addLong(325, blockLengths);
    } else {
        addLong(278, {static_cast<std::uint32_t>(
            rowsPerStrip > 0 ? rowsPerStrip : height)});
        addLong(273, std::vector<std::uint32_t>(blockLengths.size(), 0));   // patched
        addLong(279, blockLengths);
    }

    const std::uint16_t offsetTag = tiled ? 324 : 273;
    const std::size_t tagCount = tags.size() + (geo ? 2 : 0);

    std::vector<std::uint8_t> out;
    out.push_back(le ? 'I' : 'M');
    out.push_back(le ? 'I' : 'M');
    put16(out, magic, le);
    put32(out, 8, le);                                    // IFD sits right after

    const std::size_t ifdStart = out.size();
    put16(out, static_cast<std::uint16_t>(tagCount), le);
    // Reserve the entries; values larger than four bytes are appended after.
    out.resize(ifdStart + 2 + tagCount * 12 + 4, 0);

    std::vector<std::uint8_t> overflow;
    const std::size_t overflowBase = out.size();
    std::size_t entry = ifdStart + 2;
    std::size_t offsetEntryValueAt = 0;

    auto writeEntry = [&](std::uint16_t id, std::uint16_t type,
                          const std::vector<std::uint32_t>& ints,
                          const std::vector<double>& doubles) {
        const std::uint32_t count = ints.empty()
            ? static_cast<std::uint32_t>(doubles.size())
            : static_cast<std::uint32_t>(ints.size());
        const std::size_t width = type == 3 ? 2 : (type == 12 ? 8 : 4);
        put32At(out, entry, 0, le);
        out[entry]     = le ? (id & 0xff) : ((id >> 8) & 0xff);
        out[entry + 1] = le ? ((id >> 8) & 0xff) : (id & 0xff);
        out[entry + 2] = le ? (type & 0xff) : ((type >> 8) & 0xff);
        out[entry + 3] = le ? ((type >> 8) & 0xff) : (type & 0xff);
        put32At(out, entry + 4, count, le);

        std::vector<std::uint8_t> encoded;
        for (std::uint32_t v : ints) {
            if (type == 3) put16(encoded, static_cast<std::uint16_t>(v), le);
            else           put32(encoded, v, le);
        }
        for (double v : doubles) putDouble(encoded, v, le);

        if (encoded.size() <= 4) {
            for (std::size_t i = 0; i < encoded.size(); ++i)
                out[entry + 8 + i] = encoded[i];
        } else {
            put32At(out, entry + 8,
                    static_cast<std::uint32_t>(overflowBase + overflow.size()), le);
            overflow.insert(overflow.end(), encoded.begin(), encoded.end());
        }
        if (id == offsetTag) offsetEntryValueAt = entry + 8;
        (void)width;
        entry += 12;
    };

    for (const Tag& t : tags) writeEntry(t.id, t.type, t.ints, {});
    if (geo) {
        writeEntry(33550, 12, {}, {step, step, 0.0});
        writeEntry(33922, 12, {}, {0.0, 0.0, 0.0, westLon, northLat, 0.0});
    }

    out.insert(out.end(), overflow.begin(), overflow.end());

    // Now the payload, and the block offsets that point at it.
    const std::uint32_t payloadBase = static_cast<std::uint32_t>(out.size());
    out.insert(out.end(), payload.begin(), payload.end());

    std::vector<std::uint32_t> starts;
    std::uint32_t running = payloadBase;
    for (std::uint32_t length : blockLengths) {
        starts.push_back(running);
        running += length;
    }
    // The offset array is longer than four bytes whenever there is more than
    // one block, in which case the entry points into the overflow region.
    if (starts.size() == 1) {
        put32At(out, offsetEntryValueAt, starts[0], le);
    } else {
        std::uint32_t arrayAt = 0;
        for (int i = 0; i < 4; ++i)
            arrayAt |= static_cast<std::uint32_t>(out[offsetEntryValueAt + i])
                       << (le ? 8 * i : 8 * (3 - i));
        for (std::size_t i = 0; i < starts.size(); ++i)
            put32At(out, arrayAt + i * 4, starts[i], le);
    }
    return out;
}
} // namespace

