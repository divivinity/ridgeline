#pragma once
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
QString cachePath(const MapTileProvider& provider, int z, int x, int y);

} // namespace TileProviders
