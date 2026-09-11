# Ridgeline release roadmap

Priority order agreed September 6, 2026. Specter integration follows a polished
standalone release. This is a work list, not a claim that these features exist.

## Current handoff — September 9, 18:08 UTC (updated after commit 61c18fa)

- Latest verified application is **build2/isolated-qa**, not the primary build2
  executable. Last full build succeeded and all 14 suites passed in 35.20 seconds.
  Preserve any open user workspace; do not replace its executable or discard work.
- Completed increments: shared dark palette, compact source/analysis layouts,
  visible numeric step controls, accessible numeric names, unsaved-state guards,
  Unicode scenario/terrain paths, atomic scenario/CSV/PNG writes, locked-target
  preservation, multi-row CSV and exact grid-PNG sample checks. These are not a
  completed standalone release or proof of terrain/RF accuracy.
- Done since this handoff was written: the export-dialog workflow below
  (see "Export dialog verification checkpoint"). Exports now go through the
  application's own dialogs in test, with content, cancel, failure and retry
  covered on isolated synthetic data.
- Next focused workflow: map interaction and project protection — navigation
  and placement checks, recovery/autosave, and reproducible saved-terrain
  provenance. Camera/zoom preservation across the terrain picker is already
  named as pending above and is a reasonable entry point.
- After that, return to map interaction and project-protection items below:
  navigation/placement checks, recovery/autosave and reproducible saved terrain
  provenance. Do not keep repeating already-passing export-helper tests unless
  production code changes or a new failure mode warrants them.
- Outstanding broader scope still includes map-type selection, higher-resolution
  provider work, LiDAR, foliage and Windows release packaging. Keep the original
  priority list; none of these is implicitly completed by the recent polish.
- Remaining QA limitations: real native file-dialog/title-bar checks, focus and
  screen-reader audit, GDAL GeoTIFF coverage (GDAL absent), disk-full injection,
  broader terrain references and vertical-datum handling. Do not fill the host
  disk or exhaust memory to simulate failures.
- Local Qwen was last unavailable (localhost:8080 connection refused). Recent
  changes were independently verified, not delegated. No unattended helper is
  active. Check availability before claiming future helper work.
- Usage check: weekly window 97% consumed; no credits redeemed or purchased.
  Existing heartbeat should retry after limits reset if execution is exhausted.

## Original priority order

1. **Core reliability and accuracy.** Coordinates, terrain alignment, missing-data
   handling, coverage input validation and reference/measurement checks.
2. **UI overhaul.** A cohesive map-first workspace, readable typography, clear
   planning/analysis/review flows, collapsible panels and accessible colors.
3. **Map interaction.** Reliable placement/selection, navigation, 2D/3D controls,
   coordinates, scale, north indicator and measurement tools.
4. **Project protection.** Autosave, recovery, recent projects, restored layouts,
   unsaved-change and stale-result indicators.
5. **Result clarity.** Failure explanations, model and fallback provenance,
   terrain resolution, and understandable link profiles.
6. **Map types.** Satellite, street, topographic, hillshade and elevation views;
   independent contours, vegetation and RF overlays.
7. **Map/terrain definition.** Higher-resolution imagery and elevation where
   available, provider/resolution indicators, progressive loading and caching.
8. **Exports.** Verified map PNGs, metric legends, metadata, reports and GIS
   formats. Screen captures and georeferenced analysis rasters are distinct outputs.
9. **Planning conveniences.** Equipment presets, duplication, grouping, bulk
   editing, bookmarks, receivers and scenario templates.
10. **Performance and offline use.** Large scenes, responsive cancellation,
    portable data packages and reproducible offline scenarios.
11. **Windows release.** Installer/portable edition, bundled dependencies,
    clean-machine testing, help, examples and diagnostics.
12. **LiDAR.** Bare-earth and surface elevation imports, canopy-height data;
    point-cloud viewing is a later option.
13. **Foliage propagation.** Vegetation coverage, canopy height/density, seasons,
    manual areas, source quality and validated frequency-dependent attenuation.
14. **Advanced analysis.** Routes, selected-area statistics, parameter sweeps
    and improved scenario comparisons.
15. **Specter mission import/replay.** Preserve observations separately from
    emitter estimates, with timestamps, provenance and estimate-quality labels.
16. **Live Specter.** Live observations and available flight telemetry after
    the standalone release bar is met.

## First reliability pass

Implemented and regression-tested locally:

- Reject HGT void samples in the selected output area; keep valid negative
  elevations and permit valid crops that exclude voids.
- Validate streamed tile dimensions before copying pixels; reject missing
  elevation used in resampling instead of manufacturing a terrain height.
- Validate terrain shape, sample spacing, finite elevations and WGS84 reference
  before replacing the current terrain.
- Validate solver inputs and reject sources outside the analysis area in both
  solver entry points, as well as in the UI.
- Validate coverage grid geometry and buffer size before treating it as usable.

Earlier local changes also make the legend metric-specific and export the
current viewport as a PNG.

Verification on September 6, 2026:

- Windows Qt 6.8.3 / MinGW build succeeds; all 11 CTest suites pass.
- New offline streamed-terrain fixtures cover orientation, malformed tile
  dimensions, transparency, missing/corrupt cache entries and cancellation.
- Desktop coverage completes on the procedural scene. Desired power, SINR,
  J/S, fade margin and service legend states were visually checked.
- A PNG exported through the desktop menu was opened and inspected: terrain,
  coverage, source marker, legend and viewport labels are present, without
  the save dialog or surrounding application controls.
- This is a first reliability checkpoint, not release or field validation.

## UI foundation checkpoint

- Shared dark-workspace styling with readable controls and focus states.
- Two-line source cards, wider resizable source panels and a Sources toolbar
  toggle to hide/restore the planning workspace.
- Compact analysis-layer menu, named metric/opacity controls and visible
  top-view/reset-view actions.
- Source-specific elevation credits distinguish synthetic, streamed and
  imported terrain; unsolved scenes no longer display a result legend.
- Windows GUI-subsystem build removes the extra console window; Fusion controls
  provide a consistent base for workspace styling.
- Source-list minimum height corrected so source settings, including Advanced,
  do not force a 1050 × 700 window taller than the requested size.
- Imagery opacity slider styling corrected after desktop inspection.

Verification on September 7, 2026:

- Windows Qt 6.8.3 / MinGW build succeeds. All 12 CTest suites pass, including
  the optional full MainWindow suite (21.59 seconds for the complete run).
- Automated checks cover the 1050 × 700 layout with no selection, a selected
  source and expanded Advanced settings, plus layer wiring and source-panel
  visibility. Existing unsaved-change and active-solve close guards pass.
- Desktop checks confirm top/reset camera controls, the compact Layers menu,
  source selection, expanded settings and source-panel hide/restore.
- A Balanced demo coverage solve completes; the result legend appears only
  after valid results exist. Clicking a result opens the inspector and draws
  its path. These views were inspected maximized and at the restored window size.
- Synthetic elevation is explicitly credited as demo-only in the viewport.
- This completes a UI foundation checkpoint, not the full overhaul or release
  validation. Inspector readability and unsupported MSL labels were identified
  for the follow-up pass below.

## Result inspector and elevation-label checkpoint

Implemented and verified September 7, 2026:

- Increased result-inspector labels to at least 12 px, explanations to 13 px,
  metric values to 15 px and the title to 16 px; improved secondary-text
  contrast and made numeric metrics selectable for copying.
- Enlarged profile-axis labels and kept the inspector vertically scrollable
  without horizontal clipping at compact widths.
- Removed unsupported MSL claims from source summaries, point results and the
  precise-position dialog. Synthetic heights are labeled demo-only; imported
  and streamed terrain heights explicitly have an unverified vertical datum.
  No elevation numbers or RF calculations were changed by this labeling pass.
- Added regression checks for all terrain-source label variants, georeferenced
  position-dialog wording, readable inspector fonts, compact scrolling and a
  completed solve with both docks open at 1050 × 700.
- Windows build succeeds; all 12 CTest suites pass (22.45 seconds).
- Desktop QA verified the revised source/receiver labels and position dialog,
  the larger result readouts, and scrolling to the full explanation in the
  restored window. The rebuilt app is left running on the synthetic demo.

## Terrain loading and recovery checkpoint

Implemented and verified September 7, 2026:

- Fixed cancellation of concurrent tile requests: abort callbacks can remove
  active replies immediately, so cancellation now iterates a stable snapshot.
- Honor cancellation before cache/network work; tests cover active and queued
  downloads, no partial cached responses, retry and subsequent offline reuse.
- Tile-cache writes use atomic replacement rather than truncating their target.
  This protects against partial writes; it does not validate upstream content
  or isolate custom providers that currently share a cache namespace.
- Refuse closing or starting another load/solve while terrain loading owns the
  workspace. Mark installed terrain dirty before imagery/display work so a
  later failure cannot silently discard the new scenario.
- Dismiss load progress before showing errors, and distinguish a preserved
  previous terrain from a failure after new elevation was installed.
- Location summaries use the actual loaded bounds and refresh across terrain
  types. The tooltip shows both sample spacings, grid dimensions, vertical
  reference and imagery information, explicitly distinguishing grid spacing
  from source resolution/accuracy.
- Added full-window regressions for missing terrain files, cancelled network
  loads, offline failure, retained sources/coverage, close guards and running
  coverage again after recovery. All 12 CTest suites pass (28.03 seconds).
- Desktop QA loaded the built-in 15 × 15 km Asheville area, displaying real
  elevation with Esri imagery, a 29.3 m terrain grid and the preserved emitter.
- Balanced coverage completed on that area in 36.2 seconds (360 × 360 result
  cells). Map-point inspection displayed geographic coordinates, metrics and a
  terrain path. This is workflow verification, not field/reference validation.
- Saved the scenario settings through the desktop UI to
  `build2/qa-asheville-loading-20260907.json` and verified its coordinates,
  source and terrain selection on disk. The app remains running with coverage
  displayed. Scenario files store settings, not the computed coverage raster.

## Provider-cache isolation checkpoint

- Custom terrain and imagery now use separate SHA-256 namespaces derived from
  the full URL template. Provider, dataset/style and credential changes no longer
  reuse another custom source's raw tiles at matching coordinates.
- Built-in terrain and imagery retain their portable cache layouts. Custom
  terrain never falls back to the default elevation cache.
- Regression tests cover deterministic URL-free keys, distinct layer/provider
  caches, loading custom elevation, rejecting a different dataset offline, and
  switching back to unchanged default elevation.
- Documented upgrade limitations: older custom elevation may already be in the
  default terrain tree. Its origin cannot be recovered reliably; a fresh cache
  is needed in that case. No existing cache data was deleted or migrated.
- Verification: Windows build succeeds; all 12 CTest suites pass in 25.95
  seconds. Desktop QA reopened the saved Asheville scenario with real elevation,
  Esri imagery and its 150 MHz / 50 W emitter. The rebuilt app remains running;
  coverage was not rerun in this pass (scenario files only restore settings).

## Scenario reopening and comparison-panel checkpoint

- Opening saved scenarios now shows cancellable elevation and imagery progress.
  Before terrain installation, cancellation/failure preserves sources, coverage
  and the previous title. After settings load, imagery cancellation leaves the
  opened scenario usable and reports the missing imagery distinctly.
- Errors dismiss progress first; close guards and callback cleanup cover this
  workflow. Local file decoders remain synchronous, with cancellation checked
  before committing their result, not continuously during decoding.
- Added local-server GUI regressions for elevation cancellation, offline
  failures, imagery cancellation, subsequent successful retry and clean state.
- Scenario comparison now has 12–16 px typography, wrapped captions/values,
  higher-contrast explanations and shorter baseline controls. Its automated
  310 × 480 check verifies vertical scrolling and reachable bottom controls.
- Windows build and all 12 suites passed in 36.45 seconds. Desktop visual QA
  was interrupted by the user's Escape stop; comparison visual approval is
  still pending. Do not resume desktop controls automatically after that stop.

## Calibration readability and offscreen visual checkpoint

- Calibration now matches the comparison panel's 12–16 px typography, with
  wrapped captions/values, higher-contrast help, selectable warnings and shorter
  correction controls. Calculations and correction eligibility are unchanged.
- Added a 320 × 480 regression with large sample counts and a long warning;
  bottom actions remain reachable without horizontal clipping.
- UI tests now honor an explicit `QT_QPA_PLATFORM` override. Windows offscreen
  runs load installed Segoe UI, Segoe UI Symbol and Consolas font files to avoid
  missing glyphs and invalid layout measurements. Default native runs are kept.
- Optional `RIDGELINE_QA_CAPTURE_DIR` writes synthetic-data panel previews to a
  caller-selected directory without overwriting existing captures. Reviewed
  calibration and populated comparison top/bottom renders from
  `build2/qa-panels-ffe4e8a0d53040b2b559f0848a15ca09`.
- Windows application/test build succeeds. All 11 non-desktop suites pass in
  5.48 seconds, including offscreen UI layout/signal tests. The native MainWindow
  suite was rebuilt but deliberately not rerun during this pass, to avoid
  showing windows after the user's desktop-control stop. Its previous full-run
  result is recorded above. No desktop controls were driven or app relaunched.

## Elevation-provider classification checkpoint

- Streamed terrain retains the configured provider class at load time: built-in
  Mapzen, custom Terrarium, or unspecified. Viewport credits use that stored
  classification rather than assuming all streamed terrain comes from Mapzen.
- Custom sources explicitly show attribution as unverified. This is provider
  classification, not verified dataset identity, accuracy or vertical datum.
- Regression coverage checks default/custom loading, retained classification
  after configuration changes, and matching viewport credit text.
- September 8 verification: rebuilt streamed-terrain and UI test targets;
  all 11 non-desktop CTest suites pass in 5.29 seconds. The native MainWindow
  suite and desktop visual QA were not rerun in this checkpoint.

## DTED decoding checkpoint (September 8)

- Corrected standard UHL grid-dimension offsets and signed-magnitude elevation
  decoding. Valid negative heights and negative zero are preserved; 0xFFFF voids
  reject the import instead of manufacturing a terrain height.
- Validate numeric header fields, coordinate axes/ranges and grid bounds, DSI
  and ACC signatures, complete file length before grid allocation, profile
  indices and byte-sum checksums. Sample spacing uses the actual grid midpoint.
- Added a small non-square fixture with distinct south/north and west/east
  samples, both hemispheres, negative heights, voids and malformed-file cases.
  The fixture failed against the old parser and passes with the correction.
- Rebuilt decoder/test targets; all 12 non-desktop suites pass in 6.63 seconds.
  Full application compilation reached linking but Windows denied replacement
  of the running build2/ridgeline.exe (PID 40704). It was not closed or restarted;
  the running app does not contain this fix. Application relink/relaunch and
  native import/recovery QA remain outstanding.
- Format details cross-checked against the official GDAL DTED implementation:
  https://github.com/OSGeo/gdal/blob/master/frmts/dted/dted_api.c
  and its DTED_NODATA_VALUE definition in dted_api.h. No GDAL runtime is present
  here. These synthetic regressions are not real-dataset or field validation.
- Standard UHL-first complete sequential files only; legacy/sparse variants and
  nonconforming two's-complement encoding are not automatically repaired.

## Full build verification after session recovery (September 8)

- User authorized autonomous continuation and explicitly requested closing the
  running app. Computer-use inspection found unsaved Guntersville settings;
  saved them through the close prompt to
  `build2/Guntersville-recovery-20260908-0955.json` before the app exited.
  Verified the file exists and contains the 2400 MHz / 200 W / 6 m AGL source.
  This preserves settings, not the computed coverage or inspected path.
- Both the primary `build2` application/test build and a separate
  `build2/isolated-qa` build now succeed. All 13 suites in the isolated build
  pass in 34.53 seconds, including the native MainWindow suite (24.51 seconds).
- The executable lock is resolved and the primary executable contains the DTED
  correction. The app is left closed as requested. Dedicated DTED desktop import
  and recovery, real reference data, and panel visual QA remain outstanding.

## DTED geographic-boundary regression follow-up

- Verified geographic registration and coordinate round trips at every sample
  of the non-square fixture, including independent east/west spacing.
- Added exact diagnostic checks for voids at every row/column, checksum damage
  in both profiles, truncated headers/metadata, invalid coordinate seconds and
  eastward overflow. These checks cannot pass on an unrelated parser failure.
- Verified a valid grid ending exactly at the antimeridian loads and normalizes
  its eastern endpoint to -180 degrees.
- Rebuilt the decoder suite and passed it (0.45 seconds). Test-only changes;
  no production behavior changed or desktop app launched. The previous complete
  13-suite result remains recorded above, not claimed as rerun here.

## DTED workspace recovery regression checkpoint

- Native MainWindow tests now load complete DTED fixtures containing a validly
  checksummed void or a damaged checksum into an already solved workspace.
- Verified the specific decoder diagnostic reaches the error dialog and the
  previous coverage legend, source, location summary, title and enabled run
  control survive both failures. Existing subsequent solve and clean-close
  checks cover continued operation and unchanged dirty state.
- Rebuilt the native suite and passed it in 24.51 seconds. This is an automated
  MainWindow load-path check, not manual file-picker or real-dataset validation.
  The normal application was not launched; test windows closed on completion.

## Successful DTED retry checkpoint

- Local llama/Qwen reviewed the decoder tests read-only. Its recommendations
  were rejected on verification: it mislabeled 0x8000 negative zero as void and
  overlooked the existing 32767 assertion. No model-suggested changes were
  executed. DTED void remains 0xFFFF and missing samples remain rejected.
- Extended the native MainWindow regression to repair the malformed fixture
  and retry loading it after the failure/cancellation sequence.
- Confirmed successful import reports success, clears the previous result legend,
  updates location/title, keeps run controls usable and marks the new scenario
  dirty. Cancelling the close prompt preserves the newly loaded workspace.
- Rebuilt and passed the native suite in 36.35 seconds. This invokes the real
  window load method, but does not validate the manual file picker or reference
  dataset accuracy. The test windows close; the normal application stays closed.

## Compact location-summary checkpoint

- Geographic toolbar summaries now separate the location name and area onto
  distinct lines, with nonbreaking spaces keeping the area numbers/units together.
  Location names explicitly use plain text rather than automatic rich-text parsing.
- Local llama reviewed this small layout proposal read-only and flagged narrow
  widths as a remaining risk. Added a font-metric fit assertion for the fixture's
  area line, alongside checks for plain text and the explicit line separation.
- Application and native test target rebuild succeeded; MainWindow suite passed
  in 24.57 seconds. Manual visual QA and unusually long names/large area strings
  remain outstanding; the fixture fit check is not exhaustive layout validation.

## Long-name toolbar layout fix

- Local llama proposed a long-name regression. The native test reproduced a
  real failure: a long DTED filename increased the minimum laptop window height.
- Added CompactLocationLabel: bounded two-line layout, width-aware ellipsis,
  full underlying text for accessibility and an escaped full-name tooltip.
  Long names no longer expand the toolbar vertically.
- The regression now checks a 1050 × 700 window, visible neighboring controls,
  successful import and area-line fit. An optional synthetic toolbar capture
  uses the existing RIDGELINE_QA_CAPTURE_DIR mechanism without overwriting files.
- Reviewed `build2/qa-long-location-20260908/long-location-toolbar.png`: long
  name elision, area line and adjacent controls are readable. This also exposed
  a remaining issue: tiny areas round to 0.0 × 0.0 km; use suitable units next.
- Full primary application/test rebuild succeeded; all 13 suites pass in
  30.83 seconds. Test windows closed; the normal application remains closed.

## Small-area display units checkpoint

- Toolbar and on-map terrain summaries share an area formatter. Both dimensions
  use metres if either is below 1000 m; larger areas use kilometres. Tiny positive
  dimensions show <0.1 m rather than zero, and invalid extents say unavailable.
  This is display formatting only; terrain geometry and RF calculations are unchanged.
- Added boundary, narrow-area, tiny-area and invalid-input tests. Local llama
  suggested the threshold case; its inconsistent mixed-unit expected result was
  rejected in favor of the documented shared-unit rule.
- Reviewed `build2/qa-small-area-20260908/long-location-toolbar.png`: the small
  fixture now reads 21.9 × 30.9 m, with the name and controls unclipped.
  On-map use is implemented but has not had a separate visual capture review.
- Final full primary build and all 13 suites passed in 31.68 seconds.

## On-map small-area verification

- Added a named map-summary widget and native checks that its dimensions match
  the toolbar's area text, remain visible and fit the compact viewport.
- Reviewed `build2/qa-map-summary-20260908/small-area-map-summary.png`:
  coordinates, 21.9 × 30.9 m extent and 30.9 m terrain-grid label are readable
  and unclipped. Capture uses the synthetic DTED fixture, not field data.
- Local llama flagged overflow/truncation as a remaining general test concern.
  This checkpoint verifies the compact small-area fixture, not all possible
  extents, display scales or overlay combinations.
- Application/native target build succeeded; native suite passed in 25.70 seconds.
  Normal application remains closed. The preceding full-suite result is above.

## DTED file-picker workflow regression

- Successful DTED retry now invokes the Load Terrain action and selects the
  repaired fixture through its owned Qt file dialog instead of calling the
  loader directly. A preceding picker cancellation preserves the solved
  workspace's coverage, location and title.
- Dialog driving is bounded by a timeout and handles unexpected error dialogs.
  This uses the test's non-native Qt picker; Windows shell-dialog visual QA is
  still distinct and pending.
- Local llama suggested camera, zoom and layer-state preservation checks as
  further coverage. Those are not claimed as verified by this increment.
- Rebuilt and passed the native MainWindow suite in 24.66 seconds. Usage was
  91% at pass start; no usage reset or purchase was attempted.

## Picker overlay-state preservation checkpoint

- Resumed after the usage window reset; no reset credits or purchases used.
- Implemented the layer-state portion of llama's prior review suggestion.
  Native tests cancel the terrain picker with all six overlays disabled, then
  enabled, checking every setting survives. Testing both values catches an
  accidental reset to defaults. Existing workspace preservation checks remain.
- Rebuilt and passed the native suite in 24.10 seconds. Test-only change;
  camera/zoom preservation and Windows shell-picker visual QA remain pending.

## Unsaved title-indicator checkpoint

- Main window titles now include Qt's modified-state marker. Source edits,
  terrain imports and non-undoable settings activate it; returning undo history
  to its clean state clears it only when no explicit dirty settings remain.
  Existing save/open clean transitions also clear the marker.
- Added native checks for clean startup, source undo/redo, terrain import and
  mixed dirty-state handling. Local llama identified the mixed-state edge case;
  its regression confirms clean undo history cannot hide a dirty setting.
- Application and native target build succeeded; suite passed in 25.28 seconds.
  Native title-bar visual inspection and save-indicator-specific regression
  coverage remain pending. Close guards and scenario contents are unchanged.

## Save-indicator workflow regression

- Added a native-window test with both undoable source removal and a dirty
  non-undoable setting. Cancelling Save leaves the indicator active and writes
  no file; completing Save clears it and permits a prompt-free close.
- Reloaded the saved JSON with ScenarioIO and verified the removed source stays
  removed. Local llama also emphasized parsing the written file rather than
  accepting file existence alone; this round-trip check is included.
- Native target rebuilt and suite passed in 28.62 seconds. Test-only change;
  save-write failure injection, atomic replacement and title-bar visual QA remain
  outstanding. Current save checks stream state before destruction; flush/close
  failure behavior deserves a dedicated reliability pass.

## Atomic scenario-save checkpoint

- Scenario JSON is fully serialized before opening a destination. QSaveFile
  writes to a same-directory temporary file and commits replacement only after
  successful writing; direct-write fallback is explicitly disabled. Save returns
  failure on open/write/commit errors so callers cannot mark those saves clean.
- Added dedicated tests for first save/replacement round trips, malformed UTF-8
  serialization preserving the original bytes, Windows locked-target failure,
  temporary-file cleanup and nonexistent destination directories. Local llama
  recommended preservation testing when replacement fails; that case is covered.
- ScenarioIO now depends on Qt Core; updated the three physics test targets that
  compile it and added the focused save target. No scenario schema changes.
- Isolated application/test build succeeded; all 14 suites pass in 34.91 seconds.
  The user's running primary executable was not replaced or closed and does not
  yet contain this change. Build under `build2/isolated-qa` contains it.
- Design checked against https://doc.qt.io/qt-6/qsavefile.html. Power-loss,
  disk-full and unusual/network-filesystem durability have not been fault-tested;
  atomic replacement is not a backup or autosave/recovery feature.

## Failed-save UI recovery checkpoint

- Native save workflow now creates an empty test-only directory at the selected
  new destination after picker validation, producing a deterministic open failure
  without altering system permissions or existing user files.
- Verified the write error reaches the dialog, the unsaved indicator and edited
  sources survive, and closing still prompts. Removing the empty fixture blocker
  permits a successful retry; the saved JSON parses and the indicator clears.
- Local llama reviewed the limitations. This tests UI response to destination
  failure, not disk-full/power-loss or every commit-error path; existing-byte
  preservation remains covered by the separate save tests above.
- Isolated native suite rebuilt and passed in 25.33 seconds. Running primary
  app unchanged. Test-only blocker was removed; no user data was deleted.

## Unicode paths and DTED reference checkpoint

- Reproduced a Windows save/reopen failure with Unicode directory and filename:
  QSaveFile created the correct file, but the narrow std::ifstream path could not
  reopen it. Scenario reads now use QFile with the same UTF-8/QString path handling
  as saves and check read errors. Local llama identified the relevant path area,
  but its claim that QString conversion drops characters was corrected in review.
- DTED/HGT streams now use UTF-8 filesystem paths. HGT mosaic discovery preserves
  UTF-8 on path construction and return. Added Unicode scenario, DTED filename,
  HGT directory and mosaic regressions. All 14 suites passed after these changes.
- Downloaded optional GDAL reference fixtures into ignored `build2/reference-dted`:
  `n43.dt0` from https://raw.githubusercontent.com/OSGeo/gdal/master/autotest/gdrivers/data/n43.dt0
  (SHA256 6FB965D94585ECB391F4B4194EDD7BD9AAD58C725E2CDBB376FD6F7C69688338),
  and `n43_wgs72.dt0` from the `data/dted/` directory of the same source
  (SHA256 74E76027797F5F3035EF08FC20357F1762F5D12A11CDB1C570F2865ACCE75C76).
  They are not bundled or downloaded by tests automatically.
- Optional DTED test arguments verify n43's 121 × 121 grid, 30-arcsecond spacing
  and 43N/80W to 44N/79W sample registration against GDAL's published dted.py
  geotransform expectations (accounting for half-cell pixel-area edges).
- Reference review exposed an unhandled horizontal datum. DTED now requires DSI
  WGS84 explicitly and rejects WGS72/blank datums, rather than mislabeling them.
  Both synthetic rejection and the published WGS72 fixture pass. No reprojection
  or vertical-datum conversion was added; this is not field accuracy validation.
- Final isolated application build and all 14 suites passed in 33.26 seconds;
  the additional optional two-reference run also passed after rebuilding its
  target. Running primary app was not replaced and lacks these latest fixes.

## Reference elevation checkpoint

- The optional n43 DTED reference test now checks decoded elevation values in
  north-first raster order against GDAL's published full-grid checksum (49187)
  and 5-by-5 subwindow checksum (305). Both pass, as does WGS72 rejection.
- Expectations: https://github.com/OSGeo/gdal/blob/master/autotest/gdrivers/dted.py
  Integer checksum definition: https://github.com/OSGeo/gdal/blob/master/alg/gdalchecksum.cpp
- These small checksums can collide and do not prove every elevation is correct,
  dataset accuracy, vertical reference, or RF prediction accuracy. Synthetic exact
  sample tests remain in place. Reference files remain optional and unbundled.

Next: title indicator visual QA, Windows file-picker visual QA, further reference validation and remaining
local-format load/cancel checks;
native desktop QA of the refined analysis panels remains outstanding. The user
has subsequently reauthorized desktop controls; earlier Escape notes above
describe the historical stop, not a continuing prohibition.
Specter remains deferred until the standalone release bar is met.

## Remaining release checks

### Foliage propagation: foundation — September 10

- Roadmap item 13, foundation slice. Design in
  `docs/superpowers/specs/2026-09-10-foliage-propagation-design.md`, written
  from a three-lane investigation whose every claim was put to an adversarial
  reviewer told to default to refuting. 17 claims were refuted.
- The `foliage` material has shipped in assets/materials.json all along and has
  never been reachable: both solvers call the static `defaultMaterial()` (dry
  soil) through the library reference, and `MaterialLibrary::find()` is called
  nowhere in src/. Every solve has applied dry-soil loss regardless of cover.
- Its numbers were also wrong in two opposite directions at once: coefficients
  1.8-2.2x BELOW P.833's measured specific attenuation, yet unbounded linear, so
  about 96 dB at 1 km of woodland against a measured asymptote near 26.5 dB.
- Delivered: a canopy-height grid on TerrainModel (decimetres, empty by
  default), path depth measured on the walk lineOfSightStatus already performs
  via an out-parameter, a saturating loss term, and hand-drawn vegetation areas
  that rasterise into the grid and persist in the scenario.
- The loss term sits where rainDb sits, so it reaches both solver branches.
  Rain is the existing precedent for an excess term riding on both; foliage
  added only to the analytic branch would be silently inert in Engineering mode
  beyond 1 km where ITM takes over. ITM models no land cover, so it is additive.
- Labelled as an engineering extrapolation, not a standards-conformant
  calculation: ITU-R P.833-10 has NO sanctioned terrestrial model for
  Ridgeline's geometry - extensive woodland with both terminals outside it. Its
  equation (1) is explicitly excluded for that case and equation (7) is scoped
  to at or below 1 GHz. The saturating shape is borrowed because it is
  physically right; the conformance claim is not made.
- Am is held flat above 2.2 GHz rather than extrapolated: P.833's own values are
  non-monotonic there and a monotone fit lands roughly 3x too pessimistic at
  28 GHz. The gamma anchors mix polarisations (105.9 MHz horizontal, the rest
  slant), recorded as a known limitation since the model has no polarisation
  term.
- Two defects found by testing rather than review: the solve fingerprint first
  summarised the canopy raster as sum-and-peak, which cannot tell a stand from
  the same stand moved elsewhere, so moving vegetation would not have
  invalidated coverage; and a cursor-readout test asserted initial visibility,
  which failed whenever the developer's real pointer sat over the window.
- Not established: no UI yet for drawing areas, so vegetation is reachable only
  through a scenario file. No data ingestion - LANDFIRE (CC0) and Meta/ESA
  (CC BY) are specified in the design but not built, and the CC BY layers
  require an attribution manifest in the offline package format before they can
  ship. No cover-percent modulation, no seasonal leaf state, no polarisation.
  Nothing here is validated against field measurements; honest accuracy is
  roughly 5-10 dB RMS at UHF even for site-tuned models.

### Project protection: autosave, recovery, history and layout — September 10

- Roadmap item 4, started after the five requested slices landed.
- Autosave and crash recovery: a scenario that has never been saved has no file
  to fall back on, so unsaved work was only as durable as the process holding
  it. RecoveryStore keeps a copy outside that process, written through
  ScenarioIO so it inherits the same atomic replacement, on a two-minute timer
  and only when there is unsaved work and the workspace is idle. The copy is
  cleared on a save, a deliberate discard, or declining the offer, so it can
  never resurrect work already dealt with. Recovered work stays marked unsaved.
- Recent scenarios and recent locations, capped and de-duplicated, recorded only
  on success so a failed open or cancelled load never enters the history.
  Locations within about a hundred metres count as the same place, so nudging
  the map and reloading does not fill the list with near-duplicates.
- Window geometry and dock arrangement persist between runs. The shipped
  arrangement is captured before any stored layout is applied, so Reset
  workspace layout still returns to it rather than to what was restored.
- The stale-result indicator listed under this item was built and then removed.
  It could never appear: every path that changes an input already discards the
  field outright, so Ridgeline does not permit a stale result to exist. Shipping
  it would have meant dead UI and a vacuously passing test. What survives is
  SolveSignature, tested directly, and a regression pinning the real invariant -
  after a solve, editing a source or changing propagation mode must discard the
  result. Worth revisiting as a product decision: discarding means a long solve
  is lost to a nudged spinbox, and the signature needed to mark-instead-of-discard
  now exists.
- Environment hazard, hit twice: OneDrive syncs this working tree and silently
  rolled MainWindow.cpp back to an older version while leaving the newer one in
  a "(# Edit conflict #)" copy, which the source glob then compiled alongside it
  into duplicate symbols. The conflict copy was the correct file. Build output
  under build2/ still contains dozens of such copies. Excluding this tree from
  sync would remove a real risk of silent work loss.
- Not established: no autosave interval setting, no multi-slot recovery history,
  and no test of recovery across an actual process kill - the crash case is
  simulated by leaving a window without closing it cleanly.

### LiDAR: derived elevation supported, point clouds deferred — September 10

- Roadmap item 12, the last of five slices in the order 6 -> 3 -> 2 -> 7 -> 12.
  Delivered as the tractable half, with the rest written up rather than
  half-built.
- What works: USGS 3DEP publishes 1 m bare-earth DEMs derived from LiDAR as
  GeoTIFFs. Those load through the existing GeoTIFF path, and now report their
  source resolution, so a 1 m LiDAR-derived DEM is legible as such against the
  roughly 10 m of the best streamed elevation.
- sourceResolutionM is now set by every loader, not just DTED: single-tile HGT
  takes its post spacing, an HGT mosaic reports its tiles' native spacing rather
  than the coarser output grid it resamples onto, and GeoTIFF takes its pixel
  spacing. Asserted against the committed GDAL reference: n43.dt0's 30
  arc-second posts report about 673 m, which is the east-west spacing at 43.5 N.
  sourceResolutionM reports the finer axis rather than averaging, and the first
  version of that assertion was wrong because it compared against the 926 m
  north-south figure instead.
- Deliberately not labelled as LiDAR: a 1 m GeoTIFF does not record how it was
  produced, so the application reports the resolution it can measure rather
  than a provenance it would be guessing at.
- Point clouds are not supported and are not a missing importer. LAS and LAZ
  hold unstructured returns while TerrainModel is a regular height grid that
  every solver, raycast and overlay assumes. Supporting them means a second
  terrain representation and a second render path. Deriving a DEM externally
  (PDAL, LAStools, gdal_grid) and loading the result is the supported route.
- Canopy height needs a surface model minus a bare-earth model. 3DEP publishes
  both, but Ridgeline loads one terrain at a time and has no raster arithmetic,
  so it is not possible in the application today.
- Not established: no 3DEP GeoTIFF was actually loaded end to end, because GDAL
  is not available in this environment. The GeoTIFF path is compiled and its
  suite runs only on the Ubuntu CI leg, added earlier today.

### Terrain definition: resolution reporting and finer elevation — September 10

- Roadmap item 7, the fourth of five slices in the order 6 -> 3 -> 2 -> 7 -> 12.
- Streamed elevation was floored at 8 m per pixel and capped at zoom 14, so a
  2 km analysis area fetched no finer data than a 15 km one and every extra
  output sample was interpolation. The floor is now 2 m and the cap zoom 15.
- Zoom 15 was verified against the live service rather than assumed: a zoom-15
  Terrarium tile returns 88 KB of PNG and zoom 16 returns 404, so 15 is exactly
  the right ceiling.
- TerrainModel now carries sourceResolutionM, the ground sampling distance of
  the data a grid was built from, set from the fetched zoom for streamed
  terrain and from the post spacing for DTED. Zero means unknown.
- The location tooltip previously carried a blanket disclaimer that grid
  spacing was "not a claim of source resolution". It now states the source
  resolution and says which way the grid differs: whether the extra samples are
  interpolated, whether detail is being discarded, or neither. An unknown
  source resolution is reported as unknown rather than guessed.
- Cache implication, worth knowing before shipping: the zoom a request resolves
  to determines its cache paths, so portable offline packages built for small
  areas under the old cap will miss and re-download at zoom 15. Larger areas
  resolve to the same zoom as before and are unaffected. Four test fixtures had
  to move from zoom 14 to 15 for exactly this reason.
- Not established: no higher-resolution imagery work, no progressive or
  incremental loading, and no LiDAR-derived elevation - that is item 12.

### Workspace: menus, shortcuts and layout recovery — September 10

- Roadmap item 2, the third of five slices in the order 6 -> 3 -> 2 -> 7 -> 12.
  The Layers dock from slice 6 already took a large piece of this item.
- Running coverage is the point of the application and lived only on a toolbar
  button, with no menu entry and no shortcut. It is now Analysis > Run Coverage
  on F5. Measuring joins it on M, kept in step with the toolbar toggle so
  either can be used first.
- The camera keys T and R already worked but appeared nowhere in the interface,
  so the only way to discover them was to read the README. They are now View
  menu entries carrying their shortcuts.
- Help > Keyboard Shortcuts (F1) lists everything bound anywhere, grouped by
  project, analysis and map. A regression asserts each documented action is
  reachable from a menu and carries the shortcut the dialog claims, so the list
  cannot drift from the bindings.
- View > Reset workspace layout restores the dock arrangement captured at
  startup. Panels could previously be dragged into a state with no way back
  short of restarting.
- Not established: no work on first-run guidance beyond the existing demo-terrain
  prompt, no recent-locations or recent-scenarios list, and no persistence of
  window or dock layout between runs - that last one belongs to item 4, which is
  not in the requested order and was deliberately not started.

### Map interaction: scale, north and a ruler — September 10

- Roadmap item 3, the second of five slices in the order 6 -> 3 -> 2 -> 7 -> 12.
- MapMeasure holds the geodesic maths with no widget or GL dependency, so it is
  tested headlessly: great-circle distance, initial bearing, planar distance for
  scenes without a georeference, and a 1/2/5 rounded scale bar.
- A scale bar and a north indicator are painted children of the viewport, so
  they composite into an exported PNG the way the result legend already does.
  Scale is computed at the camera's focus from the 45 degree projection and is
  labelled as such: a perspective view of a tilted plane has no single scale.
- A ruler mode measures ground distance, bearing and rise between two clicked
  terrain points, reusing the inspector's probe geometry to draw the span.
  Without a georeference it reports distance and rise but no bearing, rather
  than inventing a north that does not exist. Placement and measuring both
  consume a terrain click, so enabling one cancels the other.
- Reviewing it on screen caught a defect no test would have: the north arrow
  was 180 degrees wrong. Camera parks the eye at +Z for azimuth zero and looks
  back at the target, so the view faces -Z, and terrain rows run south to north
  with rising Z - azimuth zero is looking SOUTH. northScreenAngleDeg() now
  exposes that convention and a regression pins all four quadrants.
- Verified on screen: the scale bar reads 200 m on the 2000 m demo scene, which
  matches the camera maths; the ruler reported 779 m and a 9.0 m rise with no
  bearing on the ungeoreferenced demo terrain, as designed.
- Not established: no measurement against a surveyed reference distance, and no
  check on a georeferenced scene where the bearing path is exercised. Scale is
  accurate at the view centre only. No 2D/3D mode switch, no coordinate readout
  under the cursor, no zoom-to-cursor.

### Map layer stack, hillshade and USGS topographic — September 10

- Roadmap item 6, the first of five slices in the agreed order 6 -> 3 -> 2 -> 7
  -> 12. Design in `docs/superpowers/specs/2026-09-10-map-layers-design.md`.
- An ordered LayerStack replaces the fixed overlay checkboxes and the Layers
  menu: nine layers with per-layer visibility, opacity and drag reordering, in
  a Layers dock. The stack is Qt-Widget-free and tested headlessly.
- Hillshade is computed from true elevations and the grid's real sample
  spacing, never the vertically exaggerated display mesh. USGS topographic
  tiles are public domain; OpenStreetMap and OpenTopoMap were rejected because
  Ridgeline's batch-download-and-repackage pattern violates their tile policies.
- Visible rasters composite into one terrain texture. Coplanar passes would
  z-fight, and the mosaic was already a QImage uploaded as a single texture, so
  blending happens before upload.
- Layer state is saved in the scenario file under a new `layers` key. Older
  scenarios load with defaults; unknown ids are ignored rather than rejected.
- Five defects were found during implementation, all by building and running
  rather than by reading the design:
  1. LayerStack had one settable callback, so the panel's registration silently
     replaced the renderer's and panel toggles never reached the viewport.
  2. The panel rebuilt its rows from inside a row's own signal, deleting the
     widget that was emitting.
  3. Binding the stack in MainWindow's constructor ran GL work before the
     viewport had a context, which segfaulted. This was hidden because
     main_window_tests depends on the app target, which could not relink while
     the app was running, so ctest kept passing a stale binary.
  4. Hillshade did not render on the procedural demo scene - the scene the app
     opens with - because the basemap bailed out without a georeference.
  5. USGS answers out-of-coverage requests with HTTP 200 and a uniform fill,
     not a 404, so the planned no-coverage check never fired. Confirmed against
     the live service: an Atlantic tile is a flat pale blue at 2421 bytes.
- The spec was corrected once during implementation: it claimed a per-provider
  tile axis-order flag was needed. It is not - placeholders substitute by name
  and the existing Esri template already spells the ArcGIS row-first order - so
  the flag was dropped and a URL regression pins the ordering instead.
- Verified: all 15 suites pass in a clean tree built outside the synced folder.
  The Layers dock, its defaults and hillshade on the demo scene were reviewed on
  screen. A live USGS tile over Haywood County NC was fetched and inspected.
- Not established: USGS topographic was not exercised end to end inside the
  application against a loaded US location, only at the tile and unit level.
  Non-US users still get satellite only. No contours, slope, elevation tint,
  street map, bring-your-own-provider UI or presets. Nothing LiDAR.

### Reference-test wiring, GDAL CI and build docs — September 9

- The GDAL reference checks were never running. `add_test` registered
  `dted_decoding_tests` with no arguments, so the `argv`-gated n43 checksum and
  WGS72 rejection blocks were skipped on every ctest run, in CI and locally.
  The fixtures also lived in ignored `build2/reference-dted`, so they would
  vanish on a clean and the validation would become unreproducible.
- Both fixtures (34 KB each, SHA-256 verified against the values recorded in
  the Unicode paths checkpoint above) are now committed under `tests/data/dted`
  with a provenance README, and CMake always passes them. Verified in both
  directions: `ctest -V` shows the GDAL blocks executing, and removing a
  fixture fails the suite rather than silently reducing it to synthetic cases.
- `geotiff_tests` ran on no platform: neither CI leg installed GDAL and it is
  absent locally, so GeoTIFF had zero automated coverage. The Ubuntu leg now
  installs `libgdal-dev`, and a step after configure fails the run if
  `geotiff_tests` is not registered, because `find_package(GDAL QUIET)`
  otherwise degrades to a smaller green suite. That guard was checked locally,
  where it correctly reports the absence.
- Not yet established: the `HAVE_GDAL` code has never been compiled anywhere,
  so the Ubuntu leg is its first build. A failure there is a real finding about
  unbuilt code, not a regression from this change.
- Found while reviewing that path: `fromGeoTIFF` still substitutes 0 m for
  nodata and non-finite samples, and does not call `TerrainModel::validate()`.
  Every other loader now rejects missing elevation instead. The GeoTIFF suite
  does not exercise nodata, so enabling it in CI will not surface this. Left
  unchanged deliberately — extending the rejection policy is a product
  decision, not a mechanical fix, and it is recorded here rather than made.
- README build docs: added Linux/Ubuntu setup, the GDAL option and its effect
  on suite registration, the `-DRIDGELINE_GUI_TESTS=ON` invocation with xvfb,
  and the SRTM clone-size caveat. Suite counts stated there were measured, not
  assumed: 13 by default, 14 with GUI tests, 15 with both.
- Salvaged from `claude/session-lurc62` before that branch is deleted. Its
  run-from-the-build-directory warning was deliberately not imported: it is
  stale, since `RuntimePaths::resolve` now prefers assets beside the executable
  and falls back to the working directory. Its `ridgeline_lib` and
  `gui_test_main.cpp` references no longer match the tree either.
- All 14 suites pass locally in 23.40 seconds; git diff --check clean.

### Export dialog verification checkpoint — September 9

- Completes the handoff's next focused workflow. A solved map is now exported
  through the application's own File > Export actions, driving the real
  QFileDialog rather than calling the exporters directly.
- Fixture is a synthetic 16 x 16 one-arc-second DTED grid at 10E/45N with
  distinct positive heights, so exported coordinates and elevations cannot
  pass a range check by accident. The run is hermetic: a temporary cache in
  offline mode, so no basemap request reaches the network.
- CSV reopened from a Unicode path and parsed: twelve columns, the
  georeferenced header naming latitude/longitude/terrain_elevation_m, every
  row inside the fixture's bounds, and `nan` for the absent jammer metrics.
  Twelve fields per row also demonstrates the classic numeric locale held; a
  comma-decimal setting would split each number into extra columns.
- PNG reopened and decoded from a Unicode path. Legend and attribution are
  viewport children, so a baseline grabbed with both hidden proves the
  exported bytes actually contain them rather than only that a file appeared.
- Legend pixels are compared across two metric selections. Identical output
  would mean the exported map credits the wrong scale to the displayed field,
  which is what the previous hardcoded-dBm legend did.
- Cancelling either picker writes no file. A destination reserved as a
  directory *after* picker validation fails deterministically, reports the
  write error, leaves an earlier export byte-identical, and succeeds on retry
  once the blocker is removed. Reserving it beforehand does not work:
  QFileDialog navigates into an existing directory instead of returning it.
- Reviewed build2/qa-export-offline-200329/exported-coverage.png: the legend
  reads SNR / dB / -20 to 40 for the selected metric, attribution reads
  "Imagery unavailable - Elevation: imported DTED", and the summary reads
  328.5 x 463.0 m at a 30.9 m grid, which matches 15 intervals of one
  arc-second at 45N.
- Not established: the fixture saturates SNR across the whole area, so the
  colour ramp's mid-range is unexercised and no contour shape is verified.
  This uses the test's non-native Qt picker; Windows shell-dialog visual QA
  remains outstanding, as do disk-full injection and export cancellation
  midway through a large write.
- All 14 suites pass in 22.15 seconds; git diff --check clean.

### PNG save protection checkpoint — September 9

- Added a null-image allocation guard before the grid PNG pixel loop. Allocation
  failure is not deliberately induced (avoids exhausting host memory).
- Extended export fixture to two rows: verifies CSV row boundaries, nine-column
  records and missing-value encoding; decoded grid PNG must contain the expected
  red/green/yellow samples and transparent missing cell at exact positions.
  This is pixel-level regression coverage, not viewport/export-dialog visual QA.

- Added shared Unicode-aware atomic PNG saving using QSaveFile with direct
  fallback disabled and checked encoding/commit. Both false-color grid PNG and
  the main viewport screenshot use it; viewport capture and overlays are unchanged.
- Added Unicode-path decode checks for image dimensions, pixel color/alpha,
  null-image preservation, directory rejection and successful replacement.
  Isolated application build passed; no layout or capture composition changed.
- Windows locked-PNG regression now passes: replacement reports failure,
  original bytes and decoded pixels remain intact, no temporary files remain,
  and retry succeeds after the scoped lock is released. UI test target rebuilt
  and suite passed in 2.96 seconds; diff whitespace check passed. This is a
  test-only follow-up with no visible application changes.
- PNG disk-full fault injection and end-to-end export-dialog
  visual checks remain outstanding. Atomic replacement is not a backup system.

### CSV export protection checkpoint — September 9

- Windows locked-target follow-up: a scoped CreateFileW read handle denies
  replacement of the temporary Unicode CSV fixture. Export returns false,
  original bytes remain identical, and no temporary file remains. Releasing
  the handle permits successful replacement. Rebuilt UI tests and passed the
  suite in 2.94 seconds; git diff --check passed. No production changes in this
  follow-up, so no new visual QA was required.

- CSV now uses Unicode-aware QSaveFile with direct-write fallback disabled,
  checked writes and commit, replacing narrow-path truncating std::ofstream.
  Formatting is streamed one grid row at a time with the classic numeric locale
  so a system comma-decimal setting cannot corrupt comma-separated columns.
- Expanded the edge-cell export regression to use a fresh temporary Unicode
  path, comma-decimal locale, invalid-grid preservation, directory rejection,
  replacement success and temporary-file cleanup. Isolated build passed.
- No visible UI/layout change. Disk-full fault injection, export
  cancellation and PNG atomic-write protection remain separate work; do not
  infer those checks from these CSV cases.

### Source spinbox affordance checkpoint — September 9

- Follow-up: all ten source numeric fields now have explicit accessible names
  including units. Added nonempty-name assertions and Up/Down key-event
  round-trips on visible numeric controls, in addition to programmatic stepping.
  Isolated build and UI interaction suite pass. This changes accessibility
  metadata, not visible labels or layout; the prior visual capture still applies.
  OS-level focus navigation, pointer hit targets, and screen-reader behavior
  remain unverified; no accessibility-compliance claim is made.

- Removed the source-settings spinbox subcontrol background/border override;
  Qt Fusion now draws its native up/down buttons and arrows against the shared
  dark palette. No ranges, increments or numeric formatting changed.
- Added arrow-mode and step-up/down round-trip checks plus an optional source
  settings capture. Reviewed build2/qa-spin-20260909-1103/source-settings-spinboxes.png:
  frequency and power arrows are visible where the earlier capture showed a
  blank strip. Isolated build and UI interaction suite passed.
- Direct pointer-hit/keyboard accessibility coverage remains a separate check;
  programmatic stepping is not evidence of physical click target usability.

### Shared palette checkpoint — September 9

- Follow-up: MainWindow test startup now also uses the shared application
  palette. Rebuilt that target and passed the native guard suite in 27.03 seconds.
  Added an optional whole-workspace capture alongside the existing toolbar/map
  captures, with the same no-overwrite protection.
- Reviewed build2/qa-full-palette-20260909-0959/compact-workspace.png: toolbar
  controls fit, the long location name elides, sidebar text is readable, and
  imported-terrain attribution stays visible. This synthetic small DTED scene
  does not validate real terrain rendering, native title decoration or OS dialogs.
  Spinbox arrow affordance looks weak and should receive a focused visual pass.
- Local Qwen review was unavailable for this follow-up (localhost:8080 refused
  the connection); no helper review is claimed.

- Added an explicit shared dark Qt palette to application startup and panel UI
  tests. Window/base/button/text/highlight/tooltip and disabled text roles no
  longer depend entirely on the host accent/light palette beneath the stylesheet.
- Isolated application build and offscreen panel suite passed (1.98 seconds).
  Reviewed build2/qa-palette-20260909-0655/imagery-hidden-toolbar.png: the formerly
  light root background is dark, with readable labels and a subdued disabled
  slider retaining 50%. Added palette-role assertions. Local Qwen review called
  out disabled-text contrast, inspected in this capture.
- Native full-application/dialog visual QA of this new palette is still needed;
  test previews do not establish accessibility compliance. Primary executable
  was not replaced.

- Imagery opacity now disables with the hidden imagery layer and retains its
  value when re-enabled. Added state/value regressions and reviewed the Windows
  toolbar capture in build2/qa-opacity-20260909-native. Local Qwen's bounded
  read-only review recommended the value-retention check, which passes.
- Isolated application build passed. Windows UI interaction suite passed;
  forcing QT_QPA_PLATFORM=offscreen exited before capture with 0xc0000602 on
  two attempts (including after deployment completed). Resolved in the checkpoint
  below: the deployed directory lacked the offscreen platform plugin.

### Offscreen test environment repair — September 9

- Isolated deployment contained qwindows.dll only. Pointing the UI test at the
  matching Qt SDK platform-plugin directory made the offscreen run pass.
- CTest now obtains that path from Qt6::QOffscreenIntegrationPlugin when the
  target exists. No machine-specific path is committed and the application
  deployment/backend selection is unchanged. Reconfigured and verified the
  offscreen UI suite without a manually supplied plugin path (1.50 seconds).
- Reviewed the offscreen imagery-hidden toolbar capture. The unparented test
  toolbar inherits a light background offscreen; native application dark-theme
  appearance remains covered by the earlier native capture, not this image.

- DTED decoding has synthetic and published reference checks above; broader real
  dataset coverage and native file-picker visual QA remain. Synthetic desktop
  import/recovery and cancellation checks are covered above. GDAL GeoTIFF
  resampling/nodata/masks need a dedicated pass; GDAL is unavailable in this build.
- Custom-provider cache isolation is covered above. Review cache freshness,
  content validation and provider-specific retention policies separately; stable
  cache identity and atomic writes do not establish freshness or data accuracy.
- Extend the stored provider classification above to verified dataset identity,
  attribution and saved-scenario provenance. Custom attribution remains
  unverified, and reopening a scenario still uses current provider configuration.
- Extend loading/solve cancellation and recovery checks to additional local
  formats and the location-picker imagery stage. Scenario reopening now covers
  both elevation and imagery; these are not exhaustive workflow checks.
- Review precision and terrain applicability with reference datasets; passing
  regression tests does not establish field accuracy.
- Track and validate actual vertical-datum metadata before offering MSL/ellipsoid
  height conversions or aligning external flight altitudes. Current labels are
  deliberately native-reference/unverified, not a datum-conversion feature.
  The legacy scenario JSON field `height_msl` still needs a compatibility-aware
  schema review; its name does not establish the terrain's vertical datum.
- Verify the seven metric legends, screen capture, source placement, and
  coordinate registration on real and synthetic scenes.
- Continue analysis/review-panel polish and real-world location workflows.
  Main toolbar, source workspace and the result inspector have passed the
  compact-layout and desktop checks described above. Scenario comparison and
  calibration now have compact-layout regressions and reviewed offscreen renders;
  native desktop verification of these refinements is still pending.

## Specter reference

The initial source review found GPS-tagged Wi-Fi/BLE observations, SQLite mission
storage, RSSI-based emitter estimates, Meshtastic transport, a WinTAK bridge and
dashboard replay/exports. Its estimator's confidence is a correlation score,
not a location-error radius. Core observation records do not contain aircraft
altitude. The ATAK plugin is a scaffold; a checked-in note reports unresolved
air-to-ground packet reception. Reverify these facts when integration begins.
