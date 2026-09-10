#pragma once
#include <QImage>
#include <QString>

// A raster basemap source. Placeholders in urlTemplate are substituted by name
// ({z}, {x}, {y}), so a service's own axis order is expressed by writing the
// template the way that service spells it. Both providers here are ArcGIS REST
// services, which address tiles row before column: /{z}/{y}/{x}.
struct MapTileProvider {
    QString id;              // matches the LayerStack layer id
    QString displayName;
    QString urlTemplate;
    QString attribution;
    int maxZoom = 19;
    bool usOnly = false;
};

namespace TileProviders {

MapTileProvider esriImagery();
MapTileProvider usgsTopo();

// Look up a provider by layer id. Returns false when the id is not a raster
// provider, which is the normal case for terrain-derived and analysis layers.
bool forLayer(const QString& layerId, MapTileProvider& out);

QString tileUrl(const MapTileProvider& provider, int z, int x, int y);

// True when an image carries no cartography. USGS answers requests outside its
// coverage with HTTP 200 and a uniform no-data fill rather than a 404, so a
// successful download says nothing about whether there is a map to show.
// Sampling for any variation is what separates real cartography from a flat
// rectangle. A wholly-water area reads the same way, which is accurate enough:
// there is no topographic detail to show there either.
bool isFeatureless(const QImage& image);
QString cachePath(const MapTileProvider& provider, int z, int x, int y);

} // namespace TileProviders
