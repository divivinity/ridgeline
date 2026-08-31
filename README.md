# Ridgeline

Ridgeline is a terrain-aware RF coverage planner for desktop. It aligns a real
elevation model, satellite imagery, emitter positions, and exported coverage in
one WGS84 coordinate system.

## Core workflow

1. Choose **Location** or press `Ctrl+L`.
2. Search for a place or enter exact latitude/longitude coordinates.
3. Pick the analysis width (2–50 km) and load the area.
4. Use **+ Emitter** or **+ Jammer**, then click the terrain.
5. Select a source to edit its frequency, power, height, and optional advanced
   antenna settings.
6. Choose Quick preview, Balanced, or High accuracy and run coverage. A live
   progress bar reports completed terrain rows and elapsed time while it runs.

`Esc` cancels placement. `T` switches to a top-down view, `R` resets the camera,
the arrow keys orbit, and the mouse wheel zooms.

## Real-world data

- Elevation streams from the global Mapzen Terrain Tiles dataset in the AWS
  Open Data Registry and is cached according to normal HTTP cache behavior.
- Satellite imagery defaults to Esri World Imagery and is requested at the
  sharpest zoom that fits the renderer's 2048 px texture budget.
- Place search uses the public OpenStreetMap Nominatim service only when the
  user presses Search. Results are cached, the endpoint is configurable, and
  no autocomplete or bulk lookup is performed.
- Local DTED, SRTM HGT, and (when built with GDAL) GeoTIFF elevation files are
  still supported from **File → Load Terrain**.

Worldwide streaming uses Web Mercator coverage (approximately 85°S to 85°N).

## Optional Google Maps imagery

Ridgeline supports the official Google Maps Platform Map Tiles API. Enable the
Map Tiles API and billing for a Google Cloud project, restrict the key as
appropriate, then set:

```powershell
$env:RIDGELINE_GOOGLE_MAPS_API_KEY = "your-key"
```

Ridgeline creates the required satellite session, requests viewport copyright
metadata, and displays Google Maps attribution in the viewport. Without a key,
the app automatically uses Esri imagery.

Other optional configuration:

```powershell
$env:RIDGELINE_BASEMAP_PROVIDER = "esri" # force the no-key provider
$env:RIDGELINE_GEOCODER_URL = "https://your-nominatim.example/search"
$env:RIDGELINE_TERRAIN_URL = "https://your-tiles/terrarium/{z}/{x}/{y}.png"
```

## Build on Windows

The project requires Qt 6 with Widgets, OpenGL, OpenGLWidgets, and Network,
plus CMake and a C++17 compiler.

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\mingw_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

GDAL is optional and only required for GeoTIFF terrain loading.

## Attribution

Terrain Tiles are provided through the AWS Open Data Registry and aggregate
multiple elevation sources. Displayed map and terrain attributions remain
visible in the viewport. Place search results are © OpenStreetMap contributors.
