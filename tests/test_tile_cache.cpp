#include "data/TileBatchDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <iostream>

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

    std::cout << "Portable offline tile cache tests passed.\n";
    return 0;
}
