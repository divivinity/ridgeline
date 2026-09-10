# Map layers: hillshade and USGS topographic

Design agreed 2026-09-10 03:18 UTC. Roadmap item 6, the first of five slices in
the order 6 → 3 → 2 → 7 → 12.

## Purpose

Ridgeline can display exactly one map: streamed satellite imagery. Which
provider it uses is chosen by environment variable, so changing it means
restarting the application with a different environment. There is no way to see
the shape of the ground under a coverage result, because satellite imagery
flattens terrain.

This slice introduces a layer stack, and two layers to populate it: hillshade
derived from the elevation already loaded, and USGS topographic tiles.

## Scope

In:

- An ordered layer stack with per-layer visibility and opacity, reorderable by
  dragging. Reordering is included because paint order is what makes a stack
  meaningfully different from fixed slots.
- Hillshade, computed from the terrain grid.
- USGS topographic raster tiles.
- A Layers dock replacing the overlay checkboxes and the Layers menu.
- Layer state persisted in the scenario file.

Out, deliberately, for later slices:

- Street maps, contours, slope, aspect, elevation tint.
- Bring-your-own-provider UI, saved presets, a simple/advanced toggle,
  quick-toggle keyboard shortcuts.
- Anything LiDAR, including LiDAR-derived elevation rasters.
- Rewriting the menu bar or the wider workspace shell. That is item 2.

## Architecture

### LayerStack

A new module, `src/map/LayerStack.{h,cpp}`. It owns an ordered list of layer
descriptors and emits change signals. It has no OpenGL and no widget
dependency, so it is testable in the headless suite rather than only through a
GL context.

```
struct MapLayer {
    QString id;            // stable key, used in scenario JSON
    QString displayName;
    LayerKind kind;        // BaseRaster | TerrainDerived | Analysis
    bool visible;
    float opacity;         // 0..1
};
```

`LayerStack` exposes ordered access, `setVisible`, `setOpacity`, `move`, and a
`changed` signal. Order is the paint order, bottom first.

Default layers and order, bottom to top:

| id | kind | default |
| --- | --- | --- |
| `satellite` | BaseRaster | visible, 0.86 |
| `usgs-topo` | BaseRaster | hidden, 1.0 |
| `hillshade` | TerrainDerived | hidden, 0.6 |
| `coverage` | Analysis | visible, 1.0 |
| `comparison` | Analysis | hidden, 1.0 |
| `los` | Analysis | hidden, 1.0 |
| `pattern` | Analysis | hidden, 1.0 |
| `jamming` | Analysis | hidden, 1.0 |
| `fresnel` | Analysis | hidden, 1.0 |

Because this is a stack rather than exclusive slots, satellite and topo can
both be visible with opacity between them. No exclusivity rule is needed.

### Rendering

`ViewportWidget::paintGL` currently hardcodes a nine-line draw order. It will
walk the stack instead, dispatching to overlay objects registered by layer id.
The terrain mesh remains pinned below every layer.

Base rasters are textured onto the terrain surface. Drawing two of them as
separate coplanar passes produces z-fighting, so the visible base rasters are
**composited into a single texture** before the terrain draw, in stack order,
each modulated by its opacity. Hillshade composites into the same texture as a
multiply. When no base raster is visible, hillshade composites over a neutral
ground tone rather than nothing, so it remains usable on its own. When no
raster layer at all is visible, the terrain draws with its existing untextured
shading. Analysis layers keep their existing separate draws, which are true 3D
geometry and are unaffected.

`LOSRayOverlay` currently serves two concerns: the LOS layer and the
inspected-point probe line. `paintGL` calls its `draw` unconditionally so the
probe survives the layer being hidden. The probe moves to its own always-on
element so that a layer's visibility means exactly one thing.

`ViewportWidget::Overlay` and `OverlayToolbar`'s hand-built checkboxes are
removed; the stack replaces both. This also removes overlay wiring from
`MainWindow.cpp`, currently 1,735 lines and the largest file in `src/ui`.

### Hillshade

Standard Lambertian relief shading, illumination azimuth 315°, altitude 45°,
computed from the terrain grid.

It must be computed from **true elevations, not the exaggerated mesh**. The
viewport applies a vertical scale factor for display; shading the stretched
geometry would overstate every slope. Slope is derived from the model's real
sample spacing in metres, which differs between the east–west and north–south
axes at most latitudes.

Hillshade needs no network, so it works offline, inside portable packages, and
on the procedural demo scene.

### USGS topographic

Tiles from the USGS National Map, public domain:

```
https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer/tile/{z}/{y}/{x}
```

ArcGIS addresses tiles row before column. This needs no special handling:
`BasemapOverlay` substitutes placeholders by name, and the existing Esri
template already spells `/{z}/{y}/{x}` literally, so USGS's identical pattern
works through the same path. An earlier draft of this design proposed a
per-provider axis-order flag; it would have been an abstraction with nothing
behind it, and is not implemented. A regression pins the produced URL so a
transposed template cannot slip in later.

Coverage is US-only. Outside it, the service returns no usable tiles, and the
layer reports **no topographic coverage for this area** rather than showing a
blank map. This matters because bring-your-own-provider is deferred, so there
is no fallback for non-US users in this slice.

Caching uses the existing portable tile cache under its own `topo/usgs`
namespace, inheriting atomic writes and offline-package behaviour.

Public-domain USGS tiles were chosen deliberately. Ridgeline batch-downloads
every tile for an analysis area up front and bakes it into a redistributable
package. That is the bulk-download pattern the OpenStreetMap and OpenTopoMap
tile policies prohibit, so neither is an acceptable built-in.

### Attribution

The viewport credit line is currently a fixed two-case string. With satellite,
topo and elevation potentially active together, it becomes an aggregate
composed from the visible raster layers plus the elevation source. USGS
requires credit when its tiles are shown.

### Persistence

Layer state — id, visibility, opacity, order — is written to the scenario JSON
under a new `layers` key. Loading a scenario without that key applies the
defaults above, so existing scenario files continue to open unchanged.

Unknown layer ids in a file are ignored rather than treated as an error, so a
scenario written by a later version stays loadable.

## Testing

Headless, in the existing offscreen suites:

- `LayerStack`: default order and state, visibility and opacity changes,
  reordering, signal emission, and round-tripping through scenario JSON
  including the missing-key and unknown-id cases.
- Hillshade: shading computed on a known synthetic slope, asserting that a
  north-facing and a south-facing sample differ in the expected direction, and
  that the result does not change when the display exaggeration changes.
- USGS provider: URL construction puts row and column in ArcGIS order, and the
  cache path lands in its own namespace.

GUI, in `ui_interaction_tests`:

- The Layers panel lists every layer, toggling a row changes stack state, and
  opacity changes propagate.

Native, in `main_window_tests`:

- Layer state survives a terrain load and a scenario reopen.
- Existing overlay-state preservation coverage is rewritten against the stack.
  Those tests currently drive the `ViewportWidget::Overlay` enum and the
  `OverlayToggle%1` / `AnalysisLayer%1` object names, all of which this slice
  removes.

## Risks

**Compositing.** Blending several rasters into one terrain texture is the
largest piece of new rendering work and the main reason this slice is not
trivial. It is also unavoidable: coplanar passes z-fight.

**Test churn.** Removing the `Overlay` enum invalidates existing regressions
that Codex wrote. They must be rewritten, not deleted, and the rewrite has to
preserve what they asserted: that a layer's on-screen state matches its
control.

**Non-US topography.** Users outside the United States get satellite only. The
explicit no-coverage state keeps that honest, but it is a real gap until
bring-your-own-provider lands.

**Schema change.** The `layers` key is the first addition to the scenario
format in this line of work. The roadmap already records a pending
compatibility review of the legacy `height_msl` field; this design does not
address that, and does not depend on it.

## Deliberately unresolved

The void-rejection policy question is still open and is not part of this slice:
`fromGeoTIFF` substitutes 0 m for nodata while every other loader rejects
missing elevation. Recorded in the roadmap, awaiting a product decision.
