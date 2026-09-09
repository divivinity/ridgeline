#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <functional>
#include <vector>

class QNetworkAccessManager;

struct TileDownloadRequest {
    QUrl url;
    // Relative path inside Ridgeline's portable raw-tile cache, for example
    // terrain/12/1234/1532.png or imagery/esri/12/1234/1532.tile.
    QString cacheRelativePath;
};

struct TileDownloadResult {
    QByteArray bytes;
    QString error;
    bool fromCache = false;
};

class TileBatchDownloader {
public:
    using ProgressCallback = std::function<void(int completed, int total)>;
    using CancelCallback = std::function<bool()>;

    // Fetches requests with bounded concurrency. Cached raw tile bytes are used
    // before the network and successful network responses are persisted back to
    // disk. RIDGELINE_OFFLINE=1 forces cache-only operation.
    static std::vector<TileDownloadResult> download(
        QNetworkAccessManager& network,
        const std::vector<TileDownloadRequest>& requests,
        int maxConcurrent = 8,
        int timeoutMs = 15000,
        const ProgressCallback& progress = {},
        const CancelCallback& cancel = {});

    // RIDGELINE_CACHE_DIR overrides this location. The directory can be copied
    // between machines as an offline terrain/imagery package.
    static QString cacheRoot();
    static bool offlineMode();

    // Stable, filesystem-safe identity for the complete custom URL template.
    // Include query parameters (styles, versions, credentials); never put the
    // raw URL in cache paths. Use beneath separate terrain/imagery folders.
    static QString customProviderCacheKey(const QString& urlTemplate);
};
