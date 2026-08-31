# Ridgeline

Ridgeline is a terrain-aware RF coverage planner for desktop. It aligns elevation,
satellite imagery, emitter/jammer positions, propagation results, and exports in
one WGS84 coordinate system.

## Core workflow

1. Choose **Location** or press `Ctrl+L`.
2. Search for a place or enter exact latitude/longitude coordinates.
3. Pick the analysis width (2–50 km) and load the area. Elevation and imagery
   tile progress is visible and cancellable.
4. Use **+ Emitter** or **+ Jammer**, then click the terrain.
5. Select a source to edit frequency, power, AGL height, antenna type, gain,
   azimuth, downtilt, horizontal/vertical beamwidth, front-to-back ratio, cable
   loss, polarization, or an imported measured pattern.
6. Use **Analysis → Receiver / Service Criteria** to set receiver sensitivity,
   bandwidth, noise figure, required SINR, fade-margin target, and receive
   polarization.
7. Choose **Quick preview**, **Balanced**, or **High accuracy · ITM**, then run
   coverage. Solves show progress and can be cancelled without publishing a
   partial field.
8. Use the coverage metric selector to display desired dBm, jammer dBm, SNR,
   SINR, J/S, fade margin, or the service contour.

`Esc` cancels placement. `T` switches to a top-down view, `R` resets the camera,
the arrow keys orbit, and the mouse wheel zooms.

## Propagation modes

- **Quick preview** uses a coarse grid with terrain LOS and fast path loss.
- **Balanced** uses adaptive terrain-path sampling, Fresnel clearance, rain
  controls, reflection controls, and multi-obstacle diffraction.
- **High accuracy · ITM** uses the official NTIA Irregular Terrain Model
  (Longley-Rice) reference implementation for eligible 20 MHz–20 GHz paths,
  with Ridgeline's terrain-aware diffraction model as a fallback where an ITM
  prediction is not applicable.

Terrain path samples are chosen from path length and DEM resolution rather than
a fixed sample count, so narrow terrain features are less likely to be skipped.

Ridgeline is a planning/analysis tool. Propagation results depend on DEM quality,
material assumptions, transmitter/receiver parameters, and model applicability;
they are not a substitute for field measurements or a certified frequency-
coordination study.

## Link-budget outputs

Every solved cell can contain:

- desired received power (dBm)
- aggregate jammer power (dBm)
- SNR (dB)
- SINR (dB)
- jammer-to-signal ratio, J/S (dB)
- fade margin relative to receiver sensitivity (dB)
- in-service / out-of-service state

The service contour requires all configured criteria to pass: receiver
sensitivity, required SINR, and fade-margin target. CSV exports include these
metrics together with WGS84 coordinates and terrain elevation when the terrain
is georeferenced.

## Antenna patterns

The analytic antenna model supports gain, azimuth, downtilt, horizontal and
vertical beamwidth, front-to-back ratio, feed/cable loss, and polarization.
Measured patterns can be imported from CSV. A three-column file uses:

```text
plane,angle_deg,attenuation_db
azimuth,0,0
azimuth,90,12
azimuth,180,30
elevation,-90,30
elevation,0,0
elevation,90,30
```

`plane` may be `azimuth`/`az` or `elevation`/`el`. Two-column
`angle_deg,attenuation_db` files are treated as azimuth patterns. Imported
attenuation samples are interpolated onto one-degree tables. Scenario files
store the resulting pattern data so solves remain reproducible even if the
original CSV is later moved.

## Real-world data

- Elevation streams from the global Mapzen Terrain Tiles dataset in the AWS
  Open Data Registry.
- Satellite imagery defaults to Esri World Imagery.
- Terrain and imagery tile downloads use bounded concurrency instead of serial
  one-tile-at-a-time requests.
- Place search uses the public OpenStreetMap Nominatim service only when Search
  is pressed. Public requests are cached and rate-limited to at most one request
  per second; a custom geocoder endpoint can be configured.
- Local DTED, SRTM HGT, and (when built with GDAL) GeoTIFF DEMs are supported
  from **File → Load Terrain**.
- Analysis areas can cross the ±180° antimeridian; longitude conversion and tile
  X requests wrap continuously.

Worldwide streamed maps use Web Mercator coverage (approximately 85°S to 85°N).
Rotated/sheared GeoTIFFs are rejected rather than silently misregistered; warp
such rasters to a north-up grid before loading them.

## Portable offline terrain and imagery packages

Successful terrain and imagery tile responses are saved as raw portable files.
The cache root can be selected explicitly:

```powershell
$env:RIDGELINE_CACHE_DIR = "D:\RidgelineData\PacificPackage"
```

Load the locations you need while online. The directory can then be copied to
another machine or removable media. On the offline system point Ridgeline at
that directory and enable cache-only mode:

```powershell
$env:RIDGELINE_CACHE_DIR = "D:\RidgelineData\PacificPackage"
$env:RIDGELINE_OFFLINE = "1"
```

In offline mode Ridgeline never attempts missing bulk terrain/imagery tile
requests. A missing tile is reported as an offline cache miss. The package
contains provider-separated imagery and terrain tile trees, making its contents
inspectable and reproducible rather than relying on an opaque HTTP cache.

## Optional Google Maps imagery

Ridgeline supports the Google Maps Platform Map Tiles API. Enable the Map Tiles
API and billing for a Google Cloud project, restrict the key as appropriate,
then set:

```powershell
$env:RIDGELINE_GOOGLE_MAPS_API_KEY = "your-key"
```

Ridgeline creates the required satellite session, requests viewport copyright
metadata, and displays Google Maps attribution. Without a key the app uses Esri
World Imagery.

Other optional configuration:

```powershell
$env:RIDGELINE_BASEMAP_PROVIDER = "esri"
$env:RIDGELINE_GEOCODER_URL = "https://your-nominatim.example/search"
$env:RIDGELINE_TERRAIN_URL = "https://your-tiles/terrarium/{z}/{x}/{y}.png"
$env:RIDGELINE_TILE_URL = "https://your-imagery/{z}/{x}/{y}.jpg"
$env:RIDGELINE_TILE_ATTRIBUTION = "Your imagery provider"
```

## Scenario format

Scenario version 4 stores terrain source information, simulation quality,
receiver/service assumptions, local source coordinates, authoritative WGS84
latitude/longitude, AGL height, antenna orientation/feed settings, and imported
pattern tables. Older scenario versions remain readable.

When a georeferenced terrain or analysis extent changes, sources retain their
WGS84 positions and AGL heights and are reprojected into the new terrain. A
source that falls outside the new analysis area is preserved and the solver
refuses to silently clamp it to the terrain edge.

## Build

Ridgeline requires Qt 6 (Widgets, OpenGL, OpenGLWidgets, Network, Concurrent),
CMake, and a C++17 compiler. GDAL is optional and enables GeoTIFF loading/tests.
The build fetches pinned GLM, nlohmann/json, and NTIA ITM v1.4 sources.

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\mingw_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CI builds and tests Ridgeline on both Windows and Ubuntu. Regression coverage
includes physics, WGS84 source reprojection, antimeridian/high-latitude and
southern/western coordinates, renamed HGT files, adjacent HGT seams, GeoTIFF
geometry when GDAL is available, RF metrics/ITM/pattern import, cancellation,
and portable offline cache behavior.

## Attribution

Terrain Tiles are provided through the AWS Open Data Registry and aggregate
multiple elevation sources. Displayed map and terrain attributions remain
visible in the viewport. Place search results are © OpenStreetMap contributors.
High-accuracy propagation incorporates the U.S. National Telecommunications and
Information Administration (NTIA) Irregular Terrain Model reference
implementation, pinned to v1.4 in the build for reproducibility.
