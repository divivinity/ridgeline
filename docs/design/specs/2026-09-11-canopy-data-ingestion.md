# Canopy data ingestion

Follows `2026-09-10-foliage-propagation-design.md`, which built the attenuation
model and left data ingestion deferred. The requirement now is a vegetation
layer that **overlays the loaded terrain and shares its grid**, so canopy and
elevation are consumed by one path walk.

Every claim about a remote service below was established by calling it, not by
reading about it. The transcripts are reproduced where the result is
load-bearing.

## What the previous slice left unusable

`canopyHeightDm` exists and the solver reads it, but the only writer is
`VegetationAreas::apply`, driven by hand-drawn rectangles. A planner cannot
answer "what does the tree cover here do to my link" without first drawing the
tree cover from memory. The missing piece is a real canopy raster, geo-anchored,
resampled onto whatever terrain grid is loaded.

## Source survey — what was actually reachable

| Source | Probe result |
| --- | --- |
| LANDFIRE `CH`/`CC`/`EVH` ImageServer | **Works.** Documented below. |
| Meta/WRI CHM v2 (AWS `dataforgood-fb-data`) | Reachable. `tiles.geojson` is 15 MB; a single zoom-9 quadkey tile is 46 MB **BigTIFF** (`II+`), range-readable. |
| ESA WorldCover via Terrascope WMS | **Unreachable from this machine** (connection failed, no HTTP status). Not evidence the service is down. |
| UMD/GLAD global forest height | Guessed URLs 404. Not located. |

### LANDFIRE is the source this slice ships

`https://lfps.usgs.gov/arcgis/rest/services/Landfire_LF2024/LF2024_CH_CONUS/ImageServer`

- `exportImage` with `bbox`, `bboxSR=4326`, `imageSR=4326`, `size=W,H`,
  `format=tiff`, `f=image` returns a TIFF **covering exactly the requested
  box at exactly the requested size**. The server does the reprojection out of
  EPSG:5070 and the resampling. Confirmed: a request for
  `-78.90,35.03,-78.86,35.07` at 64×64 came back with `ModelTiepoint`
  `(0,0,0, -78.9, 35.07, 0)` and `ModelPixelScale` `0.000625` — the north-west
  corner and `0.04/64` exactly.
- Returned TIFF is little-endian, `Compression=1` (none), tiled 128×128,
  `SampleFormat=2` (signed int), `BitsPerSample=16`, one band.
- `CH` is **Forest Canopy Height in decimetres**. The legend endpoint gives
  `Forest Height 018 - <050 meters X 10` and so on, and the observed values are
  `0, 30, 70, 110, … 510` — bin centres four metres apart. So the source is
  quantised to ~4 m and tops out at 51 m.
- `CC` is **Forest Canopy Cover percent**, values `0, 15, 25, … 95`, the
  midpoints of the ten-percent classes the legend names.
- Both are `0` where non-forested and `-9999` where there is no data.
- Extent is CONUS in EPSG:5070; separate services cover AK, HI and PRVI.
- LANDFIRE is a US federal product and is public domain. **No attribution
  manifest is required**, which is exactly why it goes first: the CC BY sources
  cannot ship until the offline package format can carry a licence notice.

The decisive advantage over the alternatives is that the bbox/size contract
removes reprojection and resampling from our side entirely. We ask for the
terrain's own footprint at the terrain's own sample count and get back a grid
that already lines up.

### Why `CH` and not `EVH`

`EVH` covers all lifeforms, but its encoding has the decode hole the previous
design warned about: `13`, `14`, `15`, `17` and `100` are vegetated classes
carrying no height, and they concentrate in developed areas where links are.
`CH` carries no such hole — it is zero or a height — at the cost of describing
only tree canopy. Shrub and herb heights are the smaller term and are left to
hand-drawn areas for now.

## The byte was too small

`canopyHeightDm` is `std::uint8_t`, a 25.5 m ceiling chosen when the only writer
was a hand-drawn height. LANDFIRE reaches **510 dm (51 m)** and the tallest CONUS
class is `>= 500`. Clamping would silently halve the canopy over Douglas fir and
redwood, and canopy height is geometric: it decides whether the ray is inside
the crown at all.

Widened to `std::uint16_t`. At the 1024×1024 ceiling that is 2 MiB against the
4 MiB the `float` height grid already costs.

## Architecture

### `src/data/RasterTiff.{h,cpp}` — a baseline TIFF reader, written here

Ridgeline already reads GeoTIFF through GDAL, but that path is **optional at
configure time** and GDAL is not present on the development machine, so a canopy
feature behind it would be unbuildable for the person building it. This decoder
covers precisely the shape the service returns and refuses everything else with
a specific message rather than guessing:

- byte order `II`/`MM`, classic TIFF only (BigTIFF `43` is named and rejected)
- `Compression=1` only; any other value is reported by number
- stripped and tiled layouts, one sample per pixel
- `SampleFormat` unsigned/signed integer at 8/16/32 bits, and IEEE float at 32
- `ModelPixelScale` and `ModelTiepoint` returned for verification

Qt-free, so it is testable in the headless configure alongside `TerrainModel`.

### `src/data/CanopyRaster.{h,cpp}` — the geo-anchored layer

A canopy raster is stored in **its own geographic frame**, not on the terrain
grid, so it survives a terrain reload, a resolution change or a different
elevation provider. `sampleOnto(const TerrainModel&)` resamples it to whatever
grid is live.

Nearest-neighbour, not bilinear: a stand edge is a discontinuity, and
interpolating it invents a ramp of half-height trees that is not there. This
matches the choice already made in `canopyTopAtWorld`.

Carries `CanopyProvenance` — source id, display name, licence, required credit,
source ground sample distance, retrieval date — because the application already
refuses to display data whose origin it cannot state.

### `src/data/LandfireCanopy.{h,cpp}` — the online source

Split so the part worth testing does not need a network:

- `landfire::exportImageUrl(...)` builds the request. Pure, tested.
- `landfire::decodeHeight/decodeCover(bytes, expected bbox, size)` decodes and
  **verifies the returned georeferencing against what was asked for**, rejecting
  a mismatch instead of quietly misplacing the canopy. Pure, tested against a
  committed response from the live service.
- `LandfireCanopy::fetch(...)` is the thin Qt layer: two requests, progress and
  cancellation, honouring `RIDGELINE_CANOPY_URL` the way the terrain loader
  honours `RIDGELINE_TERRAIN_URL`.

Coverage is reported, not assumed. Outside the service extent LANDFIRE answers
`-9999`, so a request over Europe returns a raster that is entirely no-data.
`CanopyCoverage` carries the sampled fraction and the loader refuses a result
with no data at all, naming the region instead of installing an empty canopy.

### Compositing drawn areas over imported data

`VegetationAreas::apply(terrain, areas, imported)`:

1. start from the imported raster resampled onto the grid, or zeros
2. stamp drawn areas over it — **a drawn area replaces what is underneath**,
   because the user drew it knowing what was there
3. clear the grid entirely when neither source contributes, so
   `hasVegetation()` stays false and an untouched scene costs nothing

Overlapping drawn areas still take the tallest rather than summing.

A consequence worth stating: a drawn area cannot express a *clearing*, because
`VegetationArea::valid()` requires a positive height. Felling is not
representable yet.

### Cover modulation

`CC` is carried as `canopyCoverPct` parallel to the height grid and scales the
**depth**, not the coefficient: a path crossing 200 m of 60% cover accumulates
120 m of effective depth. Reading cover as the fraction of the path length
actually occupied by crown is physically interpretable and keeps the saturating
law untouched.

This is an engineering choice, not a P.833 provision. P.833 gives no cover term.
It is labelled the same way the rest of the model is.

Absent cover data means cover is unknown, and unknown is treated as 100% — the
conservative reading, and the one that preserves the previous slice's behaviour
for hand-drawn areas exactly.

### Fingerprinting

`SolveSignature` already rolls the canopy heights into an FNV-1a hash. Cover
changes the answer, so cover joins the hash. Without that, re-fetching canopy
over the same footprint from a different product year would leave stale coverage
on screen looking current.

## Testing

Headless, no network:

- TIFF decode of bytes constructed in the test: both byte orders, stripped and
  tiled, 8/16/32-bit integer and float, and a rejection for every unsupported
  case by name
- TIFF decode of a **committed real response** from the LANDFIRE service, with
  its exact known value histogram
- georeference verification rejecting a response whose tiepoint does not match
  the request
- resampling a canopy raster onto a terrain grid at a different resolution, and
  onto one that only partly overlaps it
- compositing: imported alone, drawn alone, drawn over imported, neither
- cover scaling depth, and absent cover meaning 100%
- the signature changing when only cover changes

## Not established

No field validation. The previous slice's warning stands and now has a second
layer: LANDFIRE `CH` is itself a model, quantised to about four metres, and its
vintage is a 2024 product describing conditions before that. A stand felled last
year is still there in the data. Nothing here has been compared against measured
path loss.
