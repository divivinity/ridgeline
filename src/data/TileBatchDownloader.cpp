#include "TileBatchDownloader.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

namespace {

bool envFlag(const char* name) {
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

QString fullCachePath(const QString& relativePath) {
    if (relativePath.isEmpty()) return {};
    return QDir(TileBatchDownloader::cacheRoot()).filePath(relativePath);
}

QByteArray readCache(const QString& relativePath) {
    const QString path = fullCachePath(relativePath);
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

void writeCache(const QString& relativePath, const QByteArray& bytes) {
    if (relativePath.isEmpty() || bytes.isEmpty()) return;
    const QString path = fullCachePath(relativePath);
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(bytes);
}

} // namespace

QString TileBatchDownloader::cacheRoot() {
    QString root = qEnvironmentVariable("RIDGELINE_CACHE_DIR").trimmed();
    if (!root.isEmpty()) return QDir::cleanPath(root);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("tile-packages"));
}

bool TileBatchDownloader::offlineMode() {
    return envFlag("RIDGELINE_OFFLINE");
}

std::vector<TileDownloadResult> TileBatchDownloader::download(
        QNetworkAccessManager& network,
        const std::vector<TileDownloadRequest>& requests,
        int maxConcurrent,
        int timeoutMs,
        const ProgressCallback& progress,
        const CancelCallback& cancel) {
    const int total = static_cast<int>(requests.size());
    std::vector<TileDownloadResult> results(requests.size());
    if (total == 0) {
        if (progress) progress(0, 0);
        return results;
    }

    maxConcurrent = std::clamp(maxConcurrent, 1, 16);
    timeoutMs = std::clamp(timeoutMs, 1000, 120000);

    std::vector<int> pending;
    pending.reserve(requests.size());
    int completed = 0;

    // Portable raw tile files are checked first. This keeps offline packages
    // deterministic and independent of QNetworkDiskCache's internal format.
    for (int i = 0; i < total; ++i) {
        const QByteArray cached = readCache(requests[i].cacheRelativePath);
        if (!cached.isEmpty()) {
            results[i].bytes = cached;
            results[i].fromCache = true;
            ++completed;
            if (progress) progress(completed, total);
        } else {
            pending.push_back(i);
        }
    }

    if (pending.empty()) return results;

    if (offlineMode()) {
        for (const int index : pending) {
            results[index].error = QStringLiteral("offline cache miss");
            ++completed;
            if (progress) progress(completed, total);
        }
        return results;
    }

    QEventLoop loop;
    QTimer cancelPoll;
    cancelPoll.setInterval(50);

    std::vector<QNetworkReply*> active;
    active.reserve(static_cast<size_t>(maxConcurrent));
    int nextPending = 0;
    bool cancelled = false;

    std::function<void()> launchMore;
    launchMore = [&]() {
        if (cancelled) return;
        while (static_cast<int>(active.size()) < maxConcurrent &&
               nextPending < static_cast<int>(pending.size())) {
            const int index = pending[nextPending++];
            QNetworkRequest request(requests[index].url);
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("Ridgeline-RF-Planner/1.0"));
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
            request.setTransferTimeout(timeoutMs);

            QNetworkReply* reply = network.get(request);
            active.push_back(reply);
            QObject::connect(reply, &QNetworkReply::finished, &loop,
                             [&, reply, index]() {
                const auto it = std::find(active.begin(), active.end(), reply);
                if (it != active.end()) active.erase(it);

                if (reply->error() == QNetworkReply::NoError) {
                    results[index].bytes = reply->readAll();
                    writeCache(requests[index].cacheRelativePath,
                               results[index].bytes);
                } else if (cancelled ||
                           reply->error() == QNetworkReply::OperationCanceledError) {
                    results[index].error = QStringLiteral("cancelled");
                } else {
                    results[index].error = reply->errorString();
                }
                reply->deleteLater();
                ++completed;
                if (progress) progress(completed, total);

                if (!cancelled) launchMore();
                if ((cancelled && active.empty()) || completed >= total)
                    loop.quit();
            });
        }

        if (completed >= total) loop.quit();
    };

    QObject::connect(&cancelPoll, &QTimer::timeout, &loop, [&]() {
        if (!cancel || !cancel()) return;
        cancelled = true;
        cancelPoll.stop();
        for (QNetworkReply* reply : active)
            if (reply) reply->abort();

        // Mark requests that were never launched so the result vector is
        // complete and progress reaches a stable terminal state.
        while (nextPending < static_cast<int>(pending.size())) {
            const int index = pending[nextPending++];
            results[index].error = QStringLiteral("cancelled");
            ++completed;
            if (progress) progress(completed, total);
        }
        if (active.empty()) loop.quit();
    });

    if (cancel) cancelPoll.start();
    launchMore();
    if (completed < total) loop.exec();
    cancelPoll.stop();
    return results;
}
