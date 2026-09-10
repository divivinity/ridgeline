#include "map/TileProviders.h"

#include <algorithm>
#include <cstdlib>

MapTileProvider TileProviders::esriImagery() {
    MapTileProvider p;
    p.id = "satellite";
    p.displayName = "Satellite";
    p.urlTemplate = QStringLiteral(
        "https://server.arcgisonline.com/ArcGIS/rest/services/"
        "World_Imagery/MapServer/tile/{z}/{y}/{x}");
    p.attribution = QStringLiteral(
        "Imagery © Esri, Maxar, Earthstar Geographics, and contributors");
    p.maxZoom = 19;
    return p;
}

MapTileProvider TileProviders::usgsTopo() {
    MapTileProvider p;
    p.id = "usgs-topo";
    p.displayName = "USGS topographic";
    // Public domain. Chosen over OpenStreetMap and OpenTopoMap deliberately:
    // Ridgeline batch-downloads every tile for an area and bakes it into a
    // redistributable package, which those projects' tile policies prohibit.
    p.urlTemplate = QStringLiteral(
        "https://basemap.nationalmap.gov/arcgis/rest/services/"
        "USGSTopo/MapServer/tile/{z}/{y}/{x}");
    p.attribution = QStringLiteral("Topographic map courtesy of the USGS National Map");
    p.maxZoom = 16;
    p.usOnly = true;
    return p;
}

bool TileProviders::forLayer(const QString& layerId, MapTileProvider& out) {
    if (layerId == "satellite") { out = esriImagery(); return true; }
    if (layerId == "usgs-topo") { out = usgsTopo();    return true; }
    return false;
}

QString TileProviders::tileUrl(const MapTileProvider& provider, int z, int x, int y) {
    QString url = provider.urlTemplate;
    url.replace("{z}", QString::number(z));
    url.replace("{x}", QString::number(x));
    url.replace("{y}", QString::number(y));
    return url;
}

QString TileProviders::cachePath(const MapTileProvider& provider, int z, int x, int y) {
    // Each provider gets its own namespace so two sources can never serve each
    // other's bytes at matching coordinates, and so an offline package stays
    // inspectable by provider.
    return QStringLiteral("imagery/%1/%2/%3/%4.tile")
        .arg(provider.id).arg(z).arg(x).arg(y);
}

bool TileProviders::isFeatureless(const QImage& image) {
    if (image.isNull() || image.width() < 2 || image.height() < 2) return true;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    const QRgb reference = rgb.pixel(0, 0);
    const int r0 = qRed(reference), g0 = qGreen(reference), b0 = qBlue(reference);
    // Every pixel, not a sample grid: a sparse grid can step straight over a
    // single contour line, and the blank case exits on the first differing
    // pixel anyway, so the full scan only runs when the tile really is flat.
    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            // JPEG tiles vary slightly even across a flat fill.
            if (std::abs(qRed(line[x]) - r0) > 6 || std::abs(qGreen(line[x]) - g0) > 6 ||
                std::abs(qBlue(line[x]) - b0) > 6)
                return false;
        }
    }
    return true;
}
