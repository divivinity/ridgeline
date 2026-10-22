#include "CanopyLoader.h"

#include "RasterTiff.h"
#include "TileBatchDownloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <stdexcept>

namespace {

// The three layers, in the order they are requested and reported.
constexpr landfire::Layer kLayers[] = {
    landfire::Layer::CanopyHeight,
    landfire::Layer::CanopyCover,
    landfire::Layer::VegetationHeight
};

QString cachePath(const QString& product, landfire::Region region,
                  landfire::Layer layer, const QString& url) {
    // The digest covers the whole request, so a different bounding box, size or
    // product never reuses another's bytes. The readable prefix keeps a cache
    // directory browsable, which matters when one is shipped as an offline
    // package.
    const QByteArray digest = QCryptographicHash::hash(
        url.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    return QStringLiteral("canopy/%1/%2/%3/%4.tif")
        .arg(product)
        .arg(QString::fromLatin1(landfire::regionSuffix(region)))
        .arg(QString::fromLatin1(landfire::layerCode(layer)))
        .arg(QString::fromLatin1(digest));
}

QString describeShare(int part, int whole) {
    if (whole <= 0) return QStringLiteral("none");
    const double percent = 100.0 * part / whole;
    if (percent > 0.0 && percent < 1.0) return QStringLiteral("under 1%");
    return QStringLiteral("%1%").arg(percent, 0, 'f', 0);
}

} // namespace

CanopyLoader::Result CanopyLoader::loadForTerrain(
        QNetworkAccessManager& network,
        const TerrainModel& terrain,
        const landfire::Options& options,
        const ProgressCallback& progress,
        const CancelCallback& cancel) {
    Result result;

    if (!terrain.hasGeoReference()) {
        result.error = QStringLiteral(
            "This terrain has no geographic reference, so there is no way to "
            "know which ground to ask for. Load real-world terrain first.");
        return result;
    }

    const landfire::Bounds bounds = landfire::boundsForTerrain(terrain);
    if (!bounds.valid()) {
        result.error = QStringLiteral("This terrain covers no measurable area.");
        return result;
    }

    const double centreLat = (bounds.north + bounds.south) * 0.5;
    const double centreLon = (bounds.east + bounds.west) * 0.5;
    const landfire::Region region = landfire::regionFor(centreLat, centreLon);
    if (region == landfire::Region::None) {
        result.error = QStringLiteral(
            "LANDFIRE covers the United States and its territories only, and "
            "this area is outside it. Draw vegetation by hand instead.");
        return result;
    }

    const int requestWidth  = std::clamp(terrain.width,  2, kMaxSamplesPerAxis);
    const int requestHeight = std::clamp(terrain.height, 2, kMaxSamplesPerAxis);
    const QString product = QString::fromStdString(landfire::defaultProduct());
    const QString urlOverride = qEnvironmentVariable("RIDGELINE_CANOPY_URL");

    std::vector<TileDownloadRequest> requests;
    requests.reserve(3);
    for (landfire::Layer layer : kLayers) {
        const QString url = QString::fromStdString(landfire::exportImageUrl(
            product.toStdString(), region, layer, bounds,
            requestWidth, requestHeight, urlOverride.toStdString()));
        requests.push_back({QUrl(url), cachePath(product, region, layer, url)});
    }

    // One at a time. Three image exports from one service are cheap to wait for
    // and expensive to fire in parallel at a public government endpoint, and a
    // partial result is useless anyway: all three layers describe the same
    // cells and the decode needs every one of them.
    const std::vector<TileDownloadResult> responses =
        TileBatchDownloader::download(network, requests, 1, 60000, progress, cancel);

    if (cancel && cancel()) {
        result.error = QStringLiteral("Canopy download cancelled.");
        return result;
    }
    if (responses.size() != requests.size()) {
        result.error = QStringLiteral("The canopy download did not complete.");
        return result;
    }

    RasterTiff decoded[3];
    for (std::size_t i = 0; i < responses.size(); ++i) {
        const char* layerName = landfire::layerCode(kLayers[i]);
        if (!responses[i].error.isEmpty()) {
            result.error = QStringLiteral("Could not fetch the %1 layer: %2")
                               .arg(QString::fromLatin1(layerName))
                               .arg(responses[i].error);
            return result;
        }
        if (responses[i].bytes.isEmpty()) {
            result.error = QStringLiteral("The %1 layer came back empty.")
                               .arg(QString::fromLatin1(layerName));
            return result;
        }
        try {
            const auto* begin =
                reinterpret_cast<const std::uint8_t*>(responses[i].bytes.constData());
            decoded[i] = RasterTiff::decode(
                std::vector<std::uint8_t>(begin, begin + responses[i].bytes.size()));
        } catch (const std::exception& e) {
            // A successful HTTP response may still be an error page. Do not
            // let those cached bytes make every later retry fail offline.
            QFile::remove(QDir(TileBatchDownloader::cacheRoot()).filePath(
                requests[i].cacheRelativePath));
            // A service that answers with an error page rather than a raster
            // lands here, which is the right place for it: the decoder said
            // exactly what it could not read.
            result.error = QStringLiteral("The %1 layer could not be read: %2")
                               .arg(QString::fromLatin1(layerName))
                               .arg(QString::fromUtf8(e.what()));
            return result;
        }
    }

    try {
        result.canopy = landfire::assemble(decoded[0], decoded[1], decoded[2],
                                           bounds, options, product.toStdString(),
                                           landfire::nowUtcIso8601());
    } catch (const std::exception& e) {
        result.error = QString::fromUtf8(e.what());
        return result;
    }

    const CanopyCoverage& coverage = result.canopy.coverage;
    if (coverage.withData == 0) {
        result.error = QStringLiteral(
            "LANDFIRE returned no data for this area, although it lies within "
            "%1. It may be offshore or beyond the edge of the mosaic.")
            .arg(QString::fromLatin1(landfire::regionDisplayName(region)));
        return result;
    }
    if (coverage.vegetated == 0) {
        result.error = QStringLiteral(
            "LANDFIRE reports no canopy anywhere in this area. Nothing was "
            "loaded, so any vegetation drawn by hand is left alone.");
        return result;
    }

    QStringList notes;
    if (coverage.withData < coverage.samples)
        notes << QStringLiteral("%1 of the area has no LANDFIRE data")
                     .arg(describeShare(coverage.samples - coverage.withData,
                                        coverage.samples));
    if (coverage.assignedHeight > 0 &&
        options.developed != landfire::DevelopedPolicy::TreatAsBare)
        notes << QStringLiteral(
            "%1 of the vegetated area is developed land, where LANDFIRE assigns "
            "a fixed height rather than measuring one")
            .arg(describeShare(coverage.assignedHeight, coverage.vegetated));
    result.note = notes.join(QStringLiteral("; "));

    return result;
}
