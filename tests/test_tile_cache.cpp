#include "data/TileBatchDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::cerr << "FAIL: temporary directory unavailable\n";
        return 1;
    }

    qputenv("RIDGELINE_CACHE_DIR", temp.path().toUtf8());
    qputenv("RIDGELINE_OFFLINE", "1");

    const QString relative = "terrain/9/10/11.png";
    const QString full = QDir(temp.path()).filePath(relative);
    QDir().mkpath(QFileInfo(full).absolutePath());
    QFile file(full);
    if (!file.open(QIODevice::WriteOnly)) return 1;
    file.write("cached-tile");
    file.close();

    QNetworkAccessManager network;
    std::vector<TileDownloadRequest> requests = {
        {QUrl("https://invalid.example/cache-hit"), relative},
        {QUrl("https://invalid.example/cache-miss"), "terrain/9/10/12.png"}
    };
    const auto results = TileBatchDownloader::download(network, requests);
    if (results.size() != 2u || !results[0].fromCache ||
        results[0].bytes != QByteArray("cached-tile")) {
        std::cerr << "FAIL: offline cache hit was not returned\n";
        return 1;
    }
    if (results[1].error != "offline cache miss") {
        std::cerr << "FAIL: missing offline tile did not report a cache miss\n";
        return 1;
    }
    if (TileBatchDownloader::cacheRoot() != QDir::cleanPath(temp.path())) {
        std::cerr << "FAIL: cache root override was not respected\n";
        return 1;
    }

    qunsetenv("RIDGELINE_OFFLINE");
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return 1;
    bool respond = false;
    int networkRequests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            QTcpSocket* socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            auto incoming = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, incoming] {
                *incoming += socket->readAll();
                if (!incoming->contains("\r\n\r\n") || socket->property("counted").toBool()) return;
                socket->setProperty("counted", true);
                ++networkRequests;
                if (!respond) return; // Hold all active requests until cancellation.
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 13\r\nConnection: close\r\n\r\ncomplete-tile");
                socket->disconnectFromHost();
            });
        }
    });
    std::vector<TileDownloadRequest> batch;
    for (int i = 0; i < 12; ++i)
        batch.push_back({QUrl(QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(i)),
                         QString("cancel-test/%1.bin").arg(i)});
    const auto preCancelled = TileBatchDownloader::download(
        network, batch, 8, 3000, {}, [] { return true; });
    if (networkRequests != 0) return 1;
    for (const auto& result : preCancelled)
        if (result.error != "cancelled" || !result.bytes.isEmpty()) return 1;

    bool cancel = false;
    QTimer cancelTimer;
    cancelTimer.setSingleShot(true);
    QObject::connect(&cancelTimer, &QTimer::timeout, [&] { cancel = true; });
    cancelTimer.start(150);
    QElapsedTimer elapsed;
    elapsed.start();
    int progressCompleted = 0;
    const auto cancelled = TileBatchDownloader::download(network, batch, 8, 3000,
        [&](int completed, int) { progressCompleted = completed; }, [&] { return cancel; });
    // Qt may enforce a lower per-host connection cap than our batch limit.
    if (networkRequests < 2 || networkRequests > 8 || progressCompleted != 12 || elapsed.elapsed() >= 2500) {
        std::cerr << "FAIL: bounded active/queued cancellation did not complete promptly\n";
        return 1;
    }

    for (int i = 0; i < 12; ++i) {
        if (cancelled[i].error != "cancelled" || !cancelled[i].bytes.isEmpty() ||
            QFileInfo::exists(QDir(temp.path()).filePath(batch[i].cacheRelativePath))) {
            std::cerr << "FAIL: cancellation produced an incomplete cached response\n";
            return 1;
        }
    }

    qputenv("RIDGELINE_OFFLINE", "1");
    const QString templateA = "https://invalid.example/{z}/{x}/{y}?style=a&token=test-secret";
    const QString keyA = TileBatchDownloader::customProviderCacheKey(templateA);
    const QString keyB = TileBatchDownloader::customProviderCacheKey(
        "https://invalid.example/{z}/{x}/{y}?style=b&token=test-secret");
    if (keyA != TileBatchDownloader::customProviderCacheKey(templateA) || keyA == keyB ||
        !QRegularExpression("^custom-[0-9a-f]{64}$").match(keyA).hasMatch() ||
        keyA == TileBatchDownloader::customProviderCacheKey(templateA + "-rotated") ||
        keyA == TileBatchDownloader::customProviderCacheKey("https://another.example/{z}/{x}/{y}")) {
        std::cerr << "FAIL: provider keys must be stable, distinct and URL-free\n";
        return 1;
    }
    const QString imageryA = "imagery/" + keyA + "/9/10/11.tile";
    const QString imageryB = "imagery/" + keyB + "/9/10/11.tile";
    const QString imageryFull = QDir(temp.path()).filePath(imageryA);
    QDir().mkpath(QFileInfo(imageryFull).absolutePath());
    QFile imagery(imageryFull);
    if (!imagery.open(QIODevice::WriteOnly) || imagery.write("provider-a") != 10) return 1;
    imagery.close();
    const auto isolated = TileBatchDownloader::download(network, {
        {QUrl("https://invalid.example/a"), imageryA},
        {QUrl("https://invalid.example/b"), imageryB},
        {QUrl("https://invalid.example/terrain"), "terrain/" + keyA + "/9/10/11.png"}
    });
    if (!isolated[0].fromCache || isolated[0].bytes != "provider-a" ||
        isolated[1].error != "offline cache miss" || isolated[2].error != "offline cache miss") {
        std::cerr << "FAIL: custom providers and layer types must not share cached bytes\n";
        for (const auto& result : isolated)
            std::cerr << "  cached=" << result.fromCache << " bytes=" << result.bytes.size()
                      << " error=" << result.error.toStdString() << '\n';
        return 1;
    }
    qunsetenv("RIDGELINE_OFFLINE");
    respond = true;
    const auto retried = TileBatchDownloader::download(network, batch, 8, 3000);
    for (const auto& result : retried)
        if (result.bytes != "complete-tile" || !result.error.isEmpty() || result.fromCache) return 1;
    qputenv("RIDGELINE_OFFLINE", "1");
    const auto cachedRetry = TileBatchDownloader::download(network, batch);
    for (const auto& result : cachedRetry)
        if (!result.fromCache || result.bytes != "complete-tile") return 1;
    qunsetenv("RIDGELINE_OFFLINE");
    qunsetenv("RIDGELINE_CACHE_DIR");
    std::cout << "Portable tile cache, cancellation and retry tests passed.\n";
    return 0;
}
