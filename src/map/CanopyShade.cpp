#include "map/CanopyShade.h"
#include "sim/TerrainModel.h"

#include <algorithm>
#include <cmath>

namespace {

// Pale scrub through to deep forest. Chosen to stay distinguishable from the
// coverage heatmap, which runs through warm colours, so a green wash is never
// mistaken for signal strength.
struct Stop { float atM; int r, g, b; };
constexpr Stop kRamp[] = {
    { 0.0f, 198, 220, 138},
    { 5.0f, 150, 198, 108},
    {12.0f,  92, 166,  80},
    {22.0f,  44, 124,  62},
    {40.0f,  18,  78,  44},
};

QRgb rampAt(float heightM, int alpha) {
    const float clamped = std::clamp(heightM, 0.0f, CanopyShade::kRampCeilingM);
    const Stop* low = &kRamp[0];
    const Stop* high = &kRamp[std::size(kRamp) - 1];
    for (std::size_t i = 1; i < std::size(kRamp); ++i) {
        if (clamped <= kRamp[i].atM) {
            low = &kRamp[i - 1];
            high = &kRamp[i];
            break;
        }
    }
    const float span = high->atM - low->atM;
    const float t = span > 0.0f ? (clamped - low->atM) / span : 0.0f;
    const auto mix = [t](int a, int b) {
        return static_cast<int>(std::lround(a + (b - a) * t));
    };
    return qRgba(mix(low->r, high->r), mix(low->g, high->g), mix(low->b, high->b),
                 alpha);
}

} // namespace

QImage CanopyShade::render(const TerrainModel& terrain) {
    if (!terrain.hasVegetation()) return {};
    if (terrain.width < 1 || terrain.height < 1) return {};
    if (terrain.canopyHeightDm.size() !=
        static_cast<std::size_t>(terrain.width) * terrain.height) return {};

    QImage image(terrain.width, terrain.height, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);

    bool anyPainted = false;
    for (int z = 0; z < terrain.height; ++z) {
        // Terrain row 0 is the southern edge; the image's row 0 is the north.
        const int row = terrain.height - 1 - z;
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(row));
        for (int x = 0; x < terrain.width; ++x) {
            const float heightM = terrain.canopyHeightAt(x, z);
            if (!(heightM > 0.0f)) continue;
            // Thin cover reads as thin. The floor keeps a sparse stand visible
            // rather than letting 15% cover vanish into the imagery.
            const float cover = std::clamp(terrain.canopyCoverAt(x, z), 0.0f, 1.0f);
            const int alpha = static_cast<int>(std::lround(
                255.0f * std::clamp(0.35f + 0.65f * cover, 0.0f, 1.0f)));
            line[x] = rampAt(heightM, alpha);
            anyPainted = true;
        }
    }

    // A grid that is present but entirely bare is not something to draw, and
    // returning an invisible image would make the layer look active when it has
    // nothing to show.
    return anyPainted ? image : QImage();
}
