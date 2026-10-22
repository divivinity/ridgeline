<!--
Produced by a design workflow: four read-only code mappers, three independent
designs (minimal, operator-first, risk-first), three judges scoring all three,
and a synthesis. The two load-bearing external facts were re-verified by hand
against the pinned toolchain before implementation:
  - nlohmann 3.11.3 value("canopyHeightM", 0.0f) throws type_error.302 on a
    present null and returns the default for an absent key.
  - Qt 6.8 findChild matches all direct children before recursing depth-first
    in creation order.
Line references to tests/test_main_window_guard.cpp (e.g. guard:1512) use the
numbering of another agent's uncommitted edits to that file; in the committed
file the unnamed findChild<QDoubleSpinBox*>() is at line 1414.
-->

# Vegetation stands and clearings: implementation design

## Decision summary

**Base design.** This builds on Design 1, which all three judges picked. It adds one map tool in the style of Measure, with two kinds: Draw Stands and Draw Clearings. With that tool the operator can:
- drag out an axis-aligned rectangle;
- click an area to select it;
- drag its middle to move it, or a corner to resize it;
- press Enter to edit its name, kind and height in a small modal dialog;
- press Delete to remove it;
- undo any of these.

Clearings are a `kind` field appended to `VegetationArea`. Drawn areas are shown by a draped outline overlay whose geometry comes from a Qt-free, headless-tested builder. Undo reuses the source snapshot command, turned into a template. Files keep world metres as the stored form. Areas are carried across georeferenced terrain changes by latitude and longitude inside `SimEngine::setTerrain`.

Neither `tests/test_main_window_guard.cpp` nor `docs/ROADMAP.md` is edited.

**Grafted improvements.** Each was agreed by at least two judges, or is needed to close a blocking concern.

| Graft | Source | Why |
|---|---|---|
| Swallow the rest of a gesture until left release, after any cancel (Escape, undo, reload, focus loss, right or middle press) | D3; judges 1, 2, 3 | Without it, `mouseMoveEvent` falls through to orbit (ViewportWidget.cpp:590-604) and the release falls into click routing (365-385). |
| Cancel the gesture in `focusOutEvent`, and on a move with no left button held | D3; judges 1, 3 | A modal dialog (F5 progress, F1) can swallow the release. |
| Stamp each gesture with `canopyRevision()` at press; the viewport refuses to emit a commit if it changed | D3; judges 1, 3 | Defence in depth on top of the cancel-on-refresh. |
| Cursor resolution order: terrain hit, then the horizontal plane at the anchor's height, then the last good point. A release that resolves only to the last good point commits nothing | D3; judges 1, 2, 3 | Stops a grazing-angle release dropping a corner kilometres away. |
| Deferred, coalescing wash refresh on a 120 ms restartable single-shot timer (`VegetationWashTimer`), flushed before PNG export | D3; judges 1, 3 | Keeps `refreshLayers()` and its nested tile loops out of `QUndoStack::push`. Holding Ctrl+Z costs one basemap rebuild. |
| Re-entrancy guard in `ViewportWidget::applyRasterLayers` | D1, D3 | The nested `QEventLoop`s (TileBatchDownloader.cpp:120,193; BasemapOverlay.cpp:45-51) deliver input and timers. |
| `HeatmapOverlay::hasField()`, `ViewportWidget::coverageFieldDrawn()`, `vegetationWashUpToDate()`, `vegetationOutlineVertexCount()` | D3; judges 1, 3 | GUI tests can then prove behaviour instead of leaning on `isLayerVisible`, which ignores `hasTexture` (ViewportWidget.cpp:766-776). |
| Pin the guard test's unstated dependencies in a new GUI test file | D3; all judges | Covers the unnamed `findChild<QListWidget*>` (guard:131) and `findChild<QDoubleSpinBox*>` (guard:1512), the single `QUndoStack`, the first "Remove" `QPushButton`, 10 layer rows, and action-text substrings. |
| Skip the canopy rebuild, and the result invalidation, when an edit leaves every footprint unchanged (e.g. a rename) | D2/D3; judges 2, 3 | `SolveSignature` hashes the grid (SolveSignature.cpp), so a rename cannot make a result stale. |
| Report how many drawn areas fell outside a newly loaded terrain, and offer "Delete Areas Outside This Terrain" | D3; judges 2, 3 | There is no list panel, so this is the only way to see and reach areas carried out of view. |
| Selection status line and dialog state the solver's actual footprint ("solves as 120 × 90 m, 12 × 9 samples"). A dim footprint rectangle is drawn around the selection | D3 `effects()`; judge 3 | The outline and the solver can never disagree without the operator seeing it. |
| One shared cell-triangulation header used by TerrainMesh, BasemapOverlay, HeatmapOverlay and the outline builder | D2; judge 1 | Outline heights can never drift from the drawn mesh. |
| `ScreenProjection` helper factored out of `antennaAtScreenPosition`, round-trip tested against `TerrainRaycast::fromScreen` | D2; judge 1 | Corner grips are picked in screen pixels, like antennas. |
| Preview and outline uploads happen only in `paintGL`, through dirty flags | D2/D3; judges 1, 3 | Never touches GL from a mouse handler, unlike ViewportWidget.cpp:497. |
| Outline depth: a true-metre lift above the basemap drape plus a pull toward the eye proportional to distance; dark halo plus light core; no x-ray pass | judges 1, 2, 3 | Resolves both the close-range and the far-range depth concerns. |
| Screen-constant dashes: along-edge distance per vertex, fragment discard, period from metres-per-pixel | D2; judge 3 | Dashed clearings stay readable at every zoom. |
| A new stand's height defaults to the median imported canopy under it when at least 50% of its samples carry imported canopy; otherwise the last height set this session (15 m to start) | D2; judges 1, 2 | Avoids a flat 15 m where measured data exists. |
| A pointer to Analysis ▸ Vegetation ▸ Draw Stands in the Load Canopy no-georeference and failure dialogs | D2; all judges | That is the moment an operator outside the US learns import will not help. |
| `minReaderVersion` ceiling on load, written only when a clearing exists | D2/D3; all judges | Future formats can be refused with a clear message. |
| `SnapshotCommand` as a header-only template in `src/ui/SnapshotCommand.h` | judges 2, 3 | Makes merge rules and interleaving testable without a display. |
| Right-drag orbits while the vegetation tool is active | new; answers judge 1's weakness | Inside the tool a left drag over terrain draws, so orbit needs another button. The right button is unused today (ViewportWidget.cpp:600-603). |

**Proposals considered and rejected**

- **Sample-centre rasterisation** (D2/D3; judge 2). Not adopted, for four reasons.
  - Two judges listed it as a blocking silent behaviour change.
  - Scenario files persist a calibration correction and its fit statistics (ScenarioIO.cpp:61-70: `desired_power_offset_db`, `rmse_before_db`, `rmse_after_db`). Those were fitted against the outward-rounded raster. Changing the raster rule silently invalidates a saved correction.
  - The mismatch is made visible instead, through the footprint rectangle and the "solves as" text.
  - Changing the rule belongs in its own slice, with a per-file rule marker.
  - The misleading "Half-open in samples" comment (VegetationAreas.cpp:61) is corrected, and the float-to-int undefined behaviour is fixed.
- **List or dock panel** (D2/D3). Deferred. The off-terrain report and delete action cover the only case that is otherwise unreachable.
- **Stored latitude/longitude per area** (D2/D3). Rejected. It causes the snap-back bug judge 2 found. Geo coordinates are used only transiently inside `setTerrain`.
- **Stand beats clearing** (D3). Rejected. Outside the US drawn stands are the only canopy, so a clearing must be able to cut them.
- **`cleanChanged` hook that clears RecoveryStore** (D3). Rejected. See below.
- **Polygons, strips, footprint fill, recomposite-only basemap, toolbar button, V shortcut, HUD hint label.** Out of scope. The HUD hint is rejected because it would be captured by PNG export through `grab()`.

**Blocking concerns and their resolution**

| Concern | Resolution (code checked) |
|---|---|
| **D3:** the `cleanChanged` handler clearing RecoveryStore deletes crash copies | Not adopted. Confirmed: MainWindow has no destructor (MainWindow.h). The guard's crash fixture creates `seed`, removes a source, calls `seed.hide()` and leaves scope (guard:1400-1417), then expects a recovery offer (guard:1438). `cleanChanged` wiring stays exactly as at MainWindow.cpp:126-128. |
| **D2:** link failures (`valid()` moved out of line; VegetationGeometry called from VegetationAreas.cpp; `RIDGELINE_PHYSICS_TEST_SOURCES`) | `valid()` and the kind string helpers stay inline in `VegetationAreas.h`, so `ScenarioIO.cpp` needs no new object in scenario_save_tests (CMakeLists.txt:211-212). New out-of-line functions in `VegetationAreas.cpp` call only `TerrainModel`. Every target that compiles `VegetationAreas.cpp` already compiles `TerrainModel.cpp`: physics sources 90-106, layer_tests 132-136, live_canopy 154-162, canopy_tests 168-171. `VegetationGeometry.cpp` is called only from the app (globbed, CMakeLists.txt:52) and the new edit test target. |
| **D2:** `refreshLayers()` inside `QUndoCommand::redo()` corrupts history through a nested loop | `applyVegetationState` never calls `refreshLayers()`. It starts `VegetationWashTimer`. The only `QEventLoop`s in `src` outside MainWindow load paths are in TileBatchDownloader.cpp:120 and BasemapOverlay.cpp:45 (grep). Everything `applyVegetationState` calls is loop-free: `setVegetation`/`rebuildCanopy` is CPU work; `refreshOverlays` and `clearAnalysisPoint` are GL uploads; the panel invalidations are widget updates. |
| **D1:** camera jump or stray click after a mid-drag cancel | `m_vegSwallowUntilRelease` is set by `cancelVegetationGesture()` whenever a gesture was active. While it is set, `mouseMoveEvent` returns before the orbit code and `mouseReleaseEvent` resets drag state and returns before click routing. A press or a buttonless move clears it, which recovers a release that went to a modal dialog. |
| **D1:** fixed 0.5 m lift z-fights far away, and the x-ray pass is clutter | Replaced by the lift-plus-eye-pull shader (see Rendering). No x-ray pass. |
| **D2/D3:** outlines depth-rejected under the basemap at close zoom | The shader lifts outline vertices 0.5 true metres. That is above basemap.vert's +0.15 m depth-writing drape, a 0.875 m rendered separation, and the pull toward the eye is added on top. |
| **D3:** a constant clip-space bias does not scale with distance | Not used. The pull is a homothety about the eye, proportional to distance. |
| **D1:** `isLayerVisible("vegetation")` assertion proves nothing | GUI tests assert `vegetationWashUpToDate()`, `coverageFieldDrawn()` and `vegetationOutlineVertexCount()`. `isLayerVisible("vegetation")` is used only as a documented proxy for `terrain().hasVegetation()` (ViewportWidget.cpp:701-713). |
| **D2/D3:** new widgets shadow the guard's unnamed `findChild` lookups | No persistent `QListWidget` or `QDoubleSpinBox` is added under MainWindow. `VegetationAreaDialog` is stack-local with parent `this` and is destroyed when `exec()` returns, like `onCanopyOptions` (MainWindow.cpp:973). Judge 1's claim that no unnamed `findChild<QDoubleSpinBox*>` exists is wrong: it is at guard:1512. A GUI pin test asserts both lookups still resolve to the Sources widgets. |
| **D2/D3:** rasterisation change alters existing results | Not adopted (see above). The outward rounding is kept and documented, and pinned by a new footprint test. |
| **D3:** stand beats clearing makes clearings useless outside the US | Clearing beats every stand, independent of order (claim buffer). |
| **D2/D3:** geo re-sync loses edits | No stored latitude/longitude. Edits change world metres only, and `setVegetation` never re-projects. |
| **D3:** `loadTerrain` misread (it has no `setPlacementMode(false)`) | Confirmed (MainWindow.cpp:1402-1464). This design adds the tool reset there explicitly. |

## Interaction

### Entry points

Everything lives in **Analysis ▸ Vegetation** (`VegetationMenu`, MainWindow.cpp:194-210). The actions are created there. They are wired after `m_viewport` exists, in the block beside the Measure wiring (MainWindow.cpp:422-441). The menu order becomes:

1. Load Canopy for This Area… (existing)
2. Clear Imported Canopy (existing)
3. Canopy Options… (existing)
4. separator
5. **Draw &Stands**: checkable, objectName `DrawVegetationStandsAction`
6. **Draw C&learings**: checkable, objectName `DrawVegetationClearingsAction`
7. **Vegetation Area &Properties…**: objectName `EditVegetationAreaAction`; enabled only with a selection
8. **&Delete Vegetation Area**: objectName `DeleteVegetationAreaAction`; enabled only with a selection
9. **Delete Areas &Outside This Terrain**: objectName `DeleteOffTerrainVegetationAction`; enabled only when `VegetationAreas::countOutside(...) > 0`
10. separator (existing)
11. disabled status action (existing); `refreshCanopyActions` appends " · N drawn area(s)" when there are any

**No shortcuts on these actions.** A window-level Delete would fire while `SourceList` has focus, and single letters collide with M, T and R. No new action text contains "Run Coverage", "Measure Distance", "Top-down view", "Reset map view" or "Keyboard Shortcuts" (guard:557-560). No toolbar button, so OverlayToolbar's 900 px width test is untouched.

**Discoverability text** in `onLoadCanopy`:
- The no-georeference dialog (MainWindow.cpp:872-875) gains a paragraph: "Anywhere, including terrain without a real-world location, you can draw stands and clearings by hand: Analysis ▸ Vegetation ▸ Draw Stands."
- The failure warning (905-911) appends: "You can draw stands and clearings by hand instead: Analysis ▸ Vegetation ▸ Draw Stands."
- No test asserts either text.

**ShortcutsDialog.cpp:**
- Line 29 becomes "Undo / redo a source or vegetation edit".
- Line 34 (Esc) becomes "Cancel a drag, deselect, then leave drawing, measuring or source placement".
- Add under Analysis: {"Delete", "Remove the selected vegetation area (while drawing vegetation)"} and {"Enter", "Edit the selected vegetation area"}.
- Map "Drag" becomes "Orbit; drag a source to move it; while drawing vegetation, drag to outline an area". Add {"Right-drag", "Orbit while drawing vegetation"}.

### Viewport API (ViewportWidget.h, next to `setMeasureMode`)

```cpp
void setVegetationDrawMode(bool active, VegetationKind kind = VegetationKind::Stand);
bool vegetationDrawMode() const;
VegetationKind vegetationDrawKind() const;
void setVegetationSelection(int index);      // mirror only; MainWindow owns selection
int  vegetationSelection() const;
void refreshVegetationAreas();               // re-read engine areas, cancel any gesture
void cancelVegetationGesture();
bool vegetationGestureActive() const;
// Test and diagnostic accessors (CPU state, valid without a frame):
int  vegetationOutlineVertexCount() const;   // vertices the current visibility gate would draw
bool coverageFieldDrawn() const;             // m_heatmap.visible && m_heatmap.hasField()
bool vegetationWashUpToDate() const;         // applyRasterLayers has run since the last canopyRevision bump
signals:
void vegetationDrawModeChanged(bool active, int kind);
void vegetationAreaDrawn(float minX, float minZ, float maxX, float maxZ);
void vegetationAreaReshaped(int index, float minX, float minZ, float maxX, float maxZ, bool resized);
void vegetationAreaClicked(int index);       // -1 = empty ground
void vegetationDeleteRequested();
void vegetationEditRequested();
void vegetationGestureRejected(QString reason);
```

`kind` is an `int` in the signal so no metatype registration is needed and tests can `invokeMethod` the slots.

### Mutual exclusion (additive; measure and placement semantics unchanged)

**`setVegetationDrawMode(true, kind)`:**
1. If measuring, call `setMeasureMode(false)`. That emits `measureModeChanged`, which keeps MeasureButton and the action in sync.
2. If placing, call `setPlacementMode(false)`, then `emit placementCancelled()`, so MainWindow's `m_placementActive` (MainWindow.cpp:556-559) never goes stale.
3. Set the tool active and the kind, and set CrossCursor.
4. Emit `vegetationDrawModeChanged(true, kind)` if either the active state or the kind changed.

**`setVegetationDrawMode(false)`:** returns if already off. Otherwise it cancels the gesture, clears the mirrored selection, calls `unsetCursor()`, and emits `vegetationDrawModeChanged(false, kind)`.

**`setMeasureMode`** (434-444) gains `if (active && m_vegetationToolActive) setVegetationDrawMode(false);`. It goes after the existing placement line and before `setCursor`, so it sits after the early return at 435. The guard's placement-then-measure sequence (guard:469-473) is untouched.

**`setPlacementMode`** (504) gains the same line as its first statement. Starting a placement therefore turns the tool off.

### MainWindow wiring

**Draw actions.** `m_drawStandsAct` `toggled(on)`:
- on: `setVegetationDrawMode(true, Stand)`;
- off, while the viewport tool is active with kind Stand: `setVegetationDrawMode(false)`.

Clearings works the same way.

**`onVegetationDrawModeChanged(bool active, int kind)`:**
- Sets both actions' checked state under `QSignalBlocker`. Checked means the tool is active and its kind matches.
- On deactivation:
  - resets `m_selectedVegetationIndex = -1`;
  - calls `refreshVegetationActions()`;
  - writes "  Ready", but only if `!m_terrainLoadInProgress`, so load-path status assertions are never clobbered.
- On activation, writes a status line:
  - Stands: "  Drawing stands · drag on the map to outline one · click an area to select it · right-drag orbits · Esc stops"
  - Clearings: "  Drawing clearings · drag to mark felled ground · a clearing removes any stand or imported canopy beneath it · Esc stops"

**Viewport signal connections**, next to MainWindow.cpp:548-559:

| Signal | Handler |
|---|---|
| `vegetationDrawModeChanged` | `onVegetationDrawModeChanged` |
| `vegetationAreaDrawn` | `onVegetationAreaDrawn` |
| `vegetationAreaReshaped` | `onVegetationAreaReshaped` |
| `vegetationAreaClicked` | `onVegetationAreaClicked` |
| `vegetationDeleteRequested` | `onDeleteVegetationArea` |
| `vegetationEditRequested` | `onEditVegetationArea` |
| `vegetationGestureRejected` | lambda that sets `"  " + reason` |

The two Delete actions and the Properties action trigger the same slots.

### Gestures

All gestures run while the tool is active, on the left button, and are handled before the existing code in each handler.

**Gesture state** (ViewportWidget private):

```cpp
struct VegetationGesture {
    enum class Kind { None, Draw, Move, Resize } kind = Kind::None;
    VegetationGeometry::Grip grip = VegetationGeometry::Grip::None;
    int index = -1;
    glm::vec3 anchor{0.0f};                 // unscaled terrain hit at press
    VegetationGeometry::Rect original{};    // Move/Resize
    VegetationGeometry::Rect preview{};
    glm::vec2 lastGood{0.0f};
    bool lastPrecise = false;
    bool dragged = false;
    std::uint64_t pressCanopyRevision = 0;
    QPoint lastCursor;
} m_vegGesture;
bool m_vegSwallowUntilRelease = false;
```

**Press** (`mousePressEvent`, 336-344)
- **A press while swallowing** means the earlier release was lost. Clear the swallow flag and continue.
- **A right or middle press while a gesture is active** calls `cancelVegetationGesture()` (which sets the swallow flag) and returns.
- **Marker hit test.** The guard at 341 becomes `e->button() == Qt::LeftButton && !m_placementActive && !m_vegetationToolActive`, so sources cannot be grabbed inside the tool.
- **Left press in the tool.** Set `m_pressPos`, `m_lastMouse`, `m_dragging = true` and `m_cameraDragging = false`, then raycast (terrain point with y).
  - **Miss:** no gesture. A drag orbits through the existing code; the release does nothing.
  - **Hit:** if a valid selection exists, project its four corners with `ScreenProjection::toScreen`. The heights come from `VegetationGeometry::meshHeightAtWorld`, and the MVP is the render MVP including vertical scale. Then call `VegetationGeometry::hitTest(selectedRect, cornersPx, cursorPx, worldHit, 10 px)`:
    - a corner grip starts **Resize**;
    - Body starts **Move**;
    - None starts **Draw**.
  - Store the anchor, the original rect, `pressCanopyRevision = m_engine->canopyRevision()`, `lastGood` and `lastPrecise = true`.

**Move** (`mouseMoveEvent`, 569-605)
- **Not dragging** (`!m_dragging`):
  - If swallowing and no left button is held, clear the swallow flag.
  - If the tool is active, set the hover cursor and return after the readout:
    - over a corner grip of the selection: SizeFDiag or SizeBDiag, chosen from the sign of the screen-space product dx·dy from the rect centre to that corner;
    - over the selection body: SizeAllCursor;
    - elsewhere: CrossCursor.
  - The existing antenna hover cursor applies only when no tool or mode is active.
- **Swallowing:** hide the readout and return, so no orbit happens.
- **Gesture active with no left button held** (release lost to a modal): cancel without swallowing and return.
- **Gesture active:**
  - Do nothing until the Manhattan distance from the press reaches `QApplication::startDragDistance()`.
  - Then set `dragged = true` and call `updateVegetationPreview(e->pos())`:
    - Resolve the point freshly, never from screen deltas:
      1. `TerrainRaycast::fromScreen` hit: precise.
      2. Otherwise `TerrainRaycast::onHorizontalPlane` at the anchor's height, clamped to the extent: precise.
      3. Otherwise `lastGood`: not precise.
    - `preview = Draw ? VegetationGeometry::fromCorners(anchor.xz, p, extent) : VegetationGeometry::reshape(original, grip, anchor.xz, p, extent)`.
    - Rebuild only the preview CPU vertex array, set `m_vegPreviewDirty`, and call `update()`. No GL and no engine call.
  - Return.
- **Right-drag orbit in the tool:** the existing orbit branch (600-603) gains `else if ((e->buttons() & Qt::RightButton) && m_vegetationToolActive) m_camera.orbit(-delta.x() * 0.4f, delta.y() * 0.4f);`.

**Wheel** (`wheelEvent`). After the existing zoom, if a dragged gesture is active, call `updateVegetationPreview(m_vegGesture.lastCursor)`. The preview stays correct as the camera changes.

**Release** (`mouseReleaseEvent`, 346-391)
- **Left release while swallowing:** clear the swallow flag, `m_dragging`, `m_cameraDragging` and `m_markerPressIndex`, and return. There is no click routing.
- **Left release in the tool:**
  1. Set `m_dragging = false`, then copy `m_vegGesture` to a local `g`. Reset the member and clear the preview (dirty) *before any emit*.
  2. `g.kind == None` (press missed terrain): reset camera drag state and return. The tool never emits `antennaClicked` or `terrainInspected`.
  3. `!g.dragged` (a click): `emit vegetationAreaClicked(VegetationGeometry::pick(areas, g.anchor.x, g.anchor.z))` and return.
  4. Otherwise resolve the release point as in Move and compute the final rect. Then:
     - If `m_engine->canopyRevision() != g.pressCanopyRevision`: emit `vegetationGestureRejected("Vegetation changed during the drag; nothing was changed")`.
     - Else if the final point is not precise: emit `vegetationGestureRejected("Released off the map; nothing was changed")`.
     - Else Draw emits `vegetationAreaDrawn(rect)`, and Move or Resize emits `vegetationAreaReshaped(g.index, rect, g.kind == Resize)`.
  5. Return.

**Keys** (`keyPressEvent`, 778-811)
- **Escape**, in order:
  1. Gesture active: `cancelVegetationGesture()`.
  2. Else the existing measure branch.
  3. Else the existing placement branch.
  4. Else tool with a selection: `emit vegetationAreaClicked(-1)`.
  5. Else tool: `setVegetationDrawMode(false)`.
  
  The vegetation branches never emit `placementCancelled`.
- **Delete or Backspace:** tool, selection, and no gesture: `emit vegetationDeleteRequested()`. Otherwise fall through to `QOpenGLWidget::keyPressEvent`.
- **Return or Enter:** tool, selection, and no gesture: `emit vegetationEditRequested()`. Otherwise fall through.

**`focusOutEvent`** (new override): `cancelVegetationGesture()`, then the base class.

**`mouseDoubleClickEvent`** is not overridden. Qt's default delivers two press/release pairs, which selects the same area twice.

**`cancelVegetationGesture()`:** if a gesture is active, set `m_vegSwallowUntilRelease = m_dragging`, reset the gesture, clear the preview (dirty) and call `update()`.

**Also cancelled by:**
- `refreshVegetationAreas()`, which `applyVegetationState` calls, so Ctrl+Z mid-drag cancels;
- `setVegetationDrawMode`;
- `reloadTerrain`;
- the `aboutToBeDestroyed` lambda.

### Hit-test rules (`VegetationGeometry`, Qt-free)

- **`pick`**: returns the smallest valid area whose closed rectangle contains the point. On equal area the later index wins. Empty ground returns -1. A copse inside a wood is therefore selectable with a click.
- **`hitTest`**:
  - A corner of the selected rect within `min(gripPx, shortestVisibleProjectedEdgePx / 3)` wins over Body.
  - Body means the world hit is inside the selected rect.
  - Otherwise None.
  - Corners that project behind the camera or off screen are ignored.
  - Corner grips ignore occlusion, like antennas. The body test is occlusion-aware because it uses the raycast.
- **Priority in the tool:** selected corner, then selected body (Move), then Draw. A click, as opposed to a drag, always re-picks. Drawing a stand nested inside the selected area therefore needs a deselect first (Esc or a click on empty ground). The selection status says so.
- **`reshape`**:
  - **Body:** translate by `cursor - anchor`. If the original rect was fully inside the extent, shift it back inside, keeping its size.
  - **Corner:** the moved corner is `clamp(cursor + (originalCorner - anchor), 0, extent)` and the opposite corner stays fixed. Min and max are re-normalised if the corner is dragged past its opposite.
- **`coversACell`:** `(maxX-minX) >= cellX && (maxZ-minZ) >= cellZ`.

### Commit handlers (new private slots in MainWindow)

**`onVegetationAreaDrawn(float minX, float minZ, float maxX, float maxZ)`**
1. Return if the tool is inactive.
2. Normalise with `fromCorners`.
3. If `!coversACell`, set status "  Area not drawn · smaller than one %1 m terrain cell" and return.
4. Build the area:
   - `kind = viewport->vegetationDrawKind()`;
   - `name = nextVegetationName(kind)`, the smallest unused "Stand N" or "Clearing N";
   - a stand's height is `defaultStandHeightM(area, fromImport)`, a clearing's is 0.
5. Push the whole vector with the new area appended, `selectedAfter = last`, description "Draw stand" or "Draw clearing".
6. Status:
   - stand from the import: "  Stand drawn · %1 m, the median imported canopy under it · Enter changes it · Ctrl+Z undoes";
   - assumed stand: "  Stand drawn · %1 m assumed, not measured · Enter sets its height · Ctrl+Z undoes";
   - clearing: "  Clearing drawn · clears %1 × %2 samples · Ctrl+Z undoes".

**`defaultStandHeightM`**
- If `m_engine.hasImportedCanopy()` and `m_engine.sampleImportedCanopy(h, c)` succeeds, compute `VegetationAreas::importedHeightUnder(terrain, area, h)`.
- If `vegetated * 2 >= samples > 0`, return the median of the vegetated samples, rounded to 0.5 m and clamped to [0.5, 100].
- Otherwise return `m_lastStandHeightM` (15 m to start; updated by dialog edits to stands).

**`onVegetationAreaReshaped(int index, float×4, bool resized)`**
- Ignored unless `index == m_selectedVegetationIndex` and it is in range.
- Rejected with status if `!coversACell`.
- No command if every bound moved by 1 mm or less.
- Otherwise push "Resize vegetation area" or "Move vegetation area" with the selection kept.

**`onVegetationAreaClicked(int index)`**
- Out of range becomes -1.
- Set `m_selectedVegetationIndex`, call `viewport->setVegetationSelection` and `refreshVegetationActions()`.
- Status:
  - with a selection: "  %name · stand %h m · %effect · drag the middle to move, a corner to resize · Enter edits · Delete removes", or "· clearing ·" for a clearing;
  - without: the tool's hint line.
- `%effect` comes from `vegetationEffectSummary()`: "solves as W × D m (X × Z samples)", or "lies outside this terrain and has no effect".

**`onDeleteVegetationArea()`**: requires a valid selection. Erase, then push "Delete vegetation area" with `selectedAfter -1`.

**`onEditVegetationArea()`**
- Requires a valid selection.
- Construct `VegetationAreaDialog dialog(area, vegetationEffectSummary(area), m_lastStandHeightM, this)` on the stack and `exec()`.
- If accepted and name, kind or height (in decimetres) changed, push "Edit vegetation area" with the selection kept.
- If the result is a stand, update `m_lastStandHeightM`.

**`onDeleteOffTerrainVegetation()`**: erase every valid area whose footprint is empty, then push "Delete off-terrain vegetation areas" with selection -1.

### VegetationAreaDialog (`src/ui/VegetationAreaDialog.{h,cpp}`)

- **Constructor:** `VegetationAreaDialog(const VegetationArea&, const QString& effectSummary, float standHeightHintM, QWidget* parent = nullptr)`.
- **objectNames:**
  - `VegetationAreaDialog` on the dialog;
  - `VegetationAreaName` (QLineEdit);
  - `VegetationAreaKind` (QComboBox: "Stand", "Clearing");
  - `VegetationAreaHeight` (QDoubleSpinBox, 0.5 to `kMaxCanopyHeightM`, 1 decimal, " m");
  - `VegetationAreaEffect` (QLabel);
  - a `QDialogButtonBox` with OK and Cancel.
- **Kind switching:** Clearing disables the height spin, with tooltip "A clearing is bare ground; its height is ignored." Switching back to Stand restores the previous value. For a clearing, the spin starts at `standHeightHintM`.
- **`area()`:** returns the original bounds with the trimmed name (the original name if left empty) and the chosen kind. The height is 0 for a clearing.
- **No SimEngine dependency.**

### Terrain, location and scenario paths

- **`loadGlobalLocation`:** after `setPlacementMode(false)` (MainWindow.cpp:1141), call `m_viewport->setVegetationDrawMode(false)`.
- **`loadTerrain`:** call `m_viewport->setVegetationDrawMode(false)` before `setTerrain` (1416).
- **`applyScenarioDoc`:** same call after `setPlacementMode(false)` (2118).
- **Wash timer:** each of the three stops `m_vegetationWashTimer` immediately before its `reloadTerrain()` (1189, 1449, 2180), because `reloadTerrain` rebuilds by force (ViewportWidget.cpp:331-332).
- **Selection:** `applyScenarioDoc` sets `m_selectedVegetationIndex = -1` beside `m_selectedIndex = -1` (2170). All three call `refreshVegetationActions()` after their tree and props refresh.
- **Status suffix:** `loadGlobalLocation`'s success status (1229-1233) and `loadTerrain`'s status (1457) append " · %1 drawn vegetation area(s) lie outside this analysis area" when `countOutside > 0`. The guard checks these strings only with `contains` (guard:691, 738, 948), and the demo scene has no areas.

## Data model and compatibility

### `src/sim/VegetationAreas.h`

```cpp
enum class VegetationKind : std::uint8_t { Stand, Clearing };

struct VegetationArea {
    float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
    float canopyHeightM = 15.0f;   // ignored for clearings
    std::string name;
    // Appended last so every aggregate initialiser {minX,minZ,maxX,maxZ,h,name}
    // (test_rf_models.cpp:787-829, test_canopy.cpp:687-713,
    // test_scenario_save.cpp:193-194) still compiles and still means a stand.
    VegetationKind kind = VegetationKind::Stand;

    bool contains(float x, float z) const;   // unchanged, closed
    // Inline on purpose: ScenarioIO calls it and scenario_save_tests links no
    // VegetationAreas.cpp. The 5e7 m cap rejects hand-edited values that would
    // otherwise reach float-to-int conversion as inf or overflow.
    bool valid() const {
        constexpr float kLimitM = 5.0e7f;
        const auto ok = [](float v) { return std::isfinite(v) && std::abs(v) <= kLimitM; };
        return ok(minX) && ok(minZ) && ok(maxX) && ok(maxZ) &&
               maxX > minX && maxZ > minZ &&
               (kind == VegetationKind::Clearing || canopyHeightM > 0.0f);  // NaN fails
    }
};

namespace VegetationAreas {
inline const char* kindToString(VegetationKind);                    // "stand" | "clearing"
inline bool kindFromString(const std::string&, VegetationKind& out);
struct Footprint { bool empty = true; int x0 = 0, x1 = -1, z0 = 0, z1 = -1;
                   std::size_t samples = 0; float minX = 0, minZ = 0, maxX = 0, maxZ = 0; };
// The samples apply() paints, and the world rectangle the solver's nearest-sample
// reads (TerrainModel.cpp canopyTopAtWorld) attribute to them, clipped to the extent.
Footprint footprint(const TerrainModel&, const VegetationArea&);
bool sameFootprint(const std::vector<VegetationArea>&, const std::vector<VegetationArea>&);
int  countOutside(const TerrainModel&, const std::vector<VegetationArea>&);
struct ImportedUnder { std::size_t samples = 0, vegetated = 0; float medianM = 0.0f; };
ImportedUnder importedHeightUnder(const TerrainModel&, const VegetationArea&,
                                  const std::vector<std::uint16_t>& baseHeightDm);
}
```

`#include <cmath>` is added. The existing "tallest wins" comment (h:66-75) gains the clearing precedence text, and the "keeps its meaning when the grid resolution changes" claim (h:16-17) is corrected to name its conditions.

### Rasterisation (`VegetationAreas.cpp`)

**Sample range (`footprint`).** Keep today's rule:
- `x0 = floor(minX / cellX)` and `x1 = ceil(maxX / cellX)`, both ends inclusive; the same for z.
- **Change:** compute in `double`, reject if `x1 < 0 || x0 > width-1`, and clamp to `[0, width-1]` *before* any int cast.
- **Replace the comment at line 61** with an honest one: the painted footprint rounds outward, so it can extend up to one sample beyond the drawn rectangle on each side. It is kept because saved scenarios, including their persisted calibration corrections, were produced with it.
- **World footprint** = `[(x0-0.5)·cellX, (x1+0.5)·cellX]` clipped to `[0, worldSize.x]`; the same for z.

**Compositing.** Replace the single `drawn` buffer with `drawn` (uint16) plus `claim` (uint8: 0 untouched, 1 stand, 2 clearing). For each valid area with a non-empty footprint:
- **Clearing:** `claim = 2`, `drawn = 0` over its samples.
- **Stand:** `value = lround(clamp(h, 0, 100) × 10)`; skip if 0, as today. Where `claim != 2`: `claim = 1` and `drawn = max(drawn, value)`.
- **Final pass:** where `claim != 0`, `heights = drawn` and `covers = 100`.
- `anyArea` now counts valid clearings.
- The existing all-zero `clearAll` and the all-100 cover drop are unchanged. A clearing that removes every vegetated sample gives `hasVegetation() == false`, and clearings on bare ground carry no grids.

**Precedence** (documented next to "tallest wins"):
- A drawn area replaces what is imported beneath it.
- Among drawn areas, a clearing beats every overlapping stand.
- Stands keep tallest-wins.
- Both rules are order-independent, so list order, undo order and file order never change the result.
- Stated limit: a copse left standing inside a clearing needs the clearing split around it.

### Engine (`SimEngine.{h,cpp}`)

**`setVegetation(areas)`:**
- If `VegetationAreas::sameFootprint(m_vegetation, areas)`, store the areas and return without `rebuildCanopy()`.
- Comment: the grid depends only on footprints, so a rename must not bump `canopyRevision`. A bump would rebuild the map texture and, through MainWindow, discard a solve whose inputs did not change.
- `sameFootprint` compares by index: sizes, then per area `kind`, `valid()`, exact bounds, and for stands `lround(clamp(h, 0, 100) × 10)`. Names are ignored.

**`setTerrain(terrain)`** (117-149), new carry for drawn areas:
- Before the swap, if `m_terrain.hasGeoReference()`, capture each area's south-west corner `(minX, minZ)` and north-east corner `(maxX, maxZ)` with `worldToLatLon`.
- After the swap, if the new terrain is also georeferenced, map both corners back with `latLonToWorld`, take min and max, and assign.
- Then call `rebuildCanopy()` as today.
- Comment: the lat/lon-to-world mapping is linear and handles latitude and longitude independently (TerrainModel.cpp:80-98), so a carried rectangle stays an exact axis-aligned rectangle on the same ground. The size in metres can change slightly between terrains linearised at different centre latitudes. If either terrain lacks a georeference, areas carry verbatim in metres, because there is no shared frame.

**`bool sampleImportedCanopy(std::vector<std::uint16_t>&, std::vector<std::uint8_t>&) const`:** calls `m_canopySampler(m_terrain, ...)` when it is set. Used only for the default stand height.

The `SimEngine.h` comment on `setVegetation` is updated to match.

### Compatibility summary

| Case | Behaviour |
|---|---|
| v5 file, this build | Entries without `kind` load as stands with identical bounds, height and name. The raster rule is unchanged, so they solve exactly as before. |
| v6 file with stands only, v5 build | Loads identically; extra keys are ignored. |
| v6 file with any clearing, v5 build | The v5 loader's `entry.value("canopyHeightM", 0.0f)` meets `null`. nlohmann 3.11.3 throws `type_error`, which ScenarioIO.cpp:307-308 turns into "Scenario parse error". Open and recovery are refused and the previous workspace is kept. |
| Future file with `minReaderVersion > 6`, this build | Refused: "This scenario needs a newer version of Ridgeline (scenario format N); this build reads format 6 and earlier". |
| Unknown `kind`, this build | Refused: "Scenario uses vegetation of kind '<kind>', which this version of Ridgeline cannot read". Silently dropping it would erase the area on the next save. |
| Non-string `kind` or non-integer `minReaderVersion` | Existing "Scenario parse error". |
| Bounds of 1e39 (inf after `value<float>`), NaN, beyond 5e7 m, or inverted | Dropped by `valid()`, as inverted areas are today. |
| Location or terrain change, both georeferenced | Areas follow the ground. |
| Location or terrain change, either side not georeferenced | Areas carry verbatim in metres, as today. |

## Rendering

### Geometry (`src/map/VegetationGeometry.{h,cpp}`, Qt-free)

**Shared triangulation.** New `src/render/TerrainTriangulation.h` holds the single definition of the cell split, `(tl, bl, tr)` and `(tr, bl, br)`:
- `appendCellIndices(idx, tl, tr, bl, br)` replaces the inline `idx.insert(... {tl, bl, tr, tr, bl, br})` in TerrainMesh.cpp:25-33, BasemapOverlay.cpp:286-293 and HeatmapOverlay.cpp:42-51.
- `heightInCell(h00, h10, h01, h11, fx, fz)` computes:
  - where `fx + fz <= 1`: `h00 + fx·(h10-h00) + fz·(h01-h00)`;
  - otherwise: `h11 + (1-fx)·(h01-h11) + (1-fz)·(h10-h11)`.
  
  Here `h10` is `heightAt(x+1, z)` (tr) and `h01` is `heightAt(x, z+1)` (bl).

`TerrainModel::heightAtWorld` is bilinear (TerrainModel.cpp:122-135) and would dip under the drawn triangles, so it is not used.

**Helpers:**
- `Rect`, `boundsOf`, `fromCorners`, `coversACell`, `pick`, `Grip`, `hitTest`, `reshape`: as in Interaction.
- `meshHeightAtWorld(terrain, x, z)`: cell index clamped to `[0, n-2]` and the fraction to `[0, 1]`, then `heightInCell`.
- `std::array<glm::vec3, 4> cornersOnMesh(terrain, rect)`: used for grip projection.

**Line vertices.** `struct LineVertex { float x, y, z, r, g, b, a, along, dashed; }`, emitted as GL_LINES pairs.

**`appendOutline(out, terrain, rect, style)`:**
- **Edge order:** `(minX,minZ)→(maxX,minZ)→(maxX,maxZ)→(minX,maxZ)→(minX,minZ)`. `along` is the horizontal distance around the unclipped perimeter.
- **Clipping:** each edge is clipped to `[0, worldSize]`, and an edge whose fixed coordinate lies off the extent is skipped. A clipped segment keeps the `along` of its unclipped position, so dashes do not jump.
- **Crossings:** a vertex at every grid-column (or row) crossing, and at every diagonal crossing where `fx = 1 - fz` (on an X edge) or `fz = 1 - fx` (on a Z edge).
- **Heights:** `meshHeightAtWorld`, with no lift; the shader applies the lift. The polyline is exact on the drawn mesh because height is linear inside each triangle.
- An area entirely off the terrain emits nothing.

**Styles** (`styleFor(kind, role)`):

| Role | Colour | Dashed |
|---|---|---|
| Committed stand | violet (0.80, 0.60, 1.00, 1.0) | no |
| Committed clearing | violet (0.80, 0.60, 1.00, 1.0) | yes |
| Selected | white (1, 1, 1, 1) | yes for a clearing |
| Solver footprint of the selection (`VegetationAreas::footprint`) | white, alpha 0.4 | no |
| Preview | white, alpha 0.85 | yes for a clearing |

Violet appears in no existing palette: heatmap ramp, comparison red/green, LOS blue/orange/red, ruler teal (LOSRayOverlay.cpp:85), markers cyan/red/amber (ViewportWidget.cpp:216-223), canopy greens. Kind never depends on colour alone.

### Overlay (`src/render/overlays/VegetationAreaOverlay.{h,cpp}`)

**API:**
- `build()`: loads `assets/shaders/outline.vert` and `outline.frag` (new; copied by the existing POST_BUILD `copy_directory`, CMakeLists.txt:73-77) and creates three VAO/VBO pairs: Committed, Selection, Preview. Attribute layout: 0 = vec3 position, 1 = vec4 colour, 2 = float along, 3 = float dashed.
- `upload(Slot, const std::vector<LineVertex>&)`.
- `draw(const DrawParams&)`: `{mvp, eyeUnscaled, viewportPx, dashPeriodM, committed, selection, preview}`.
- `destroy()` and `vertexCount(Slot)`.

**Shader.**

outline.vert:

```glsl
vec3 p = aPos + vec3(0.0, uLiftM, 0.0);          // uLiftM = 0.5 true metres
p = uEye + (p - uEye) * (1.0 - uPull);           // uPull = 0.004
vec4 clip = uMVP * vec4(p, 1.0);
clip.xy += uPixelOffset * 2.0 / uViewportPx * clip.w;
```

outline.frag:

```glsl
if (vDashed > 0.5 && fract(vAlong / uDashPeriodM) > 0.55) discard;
fragColor = (uHalo == 1) ? vec4(0.0, 0.0, 0.0, 0.6 * vColor.a) : vColor;
```

**Why these numbers.**
- **Lift.** basemap.vert:10 lifts the imagery drape 0.15 m and writes depth with polygon offset (-1, -1). A 0.5 m true lift, 1.25 m rendered under the 2.5× exaggeration, keeps outlines above the drape at close range at any angle.
- **Pull.** The pull is a homothety about the eye in unscaled space. The render MVP only scales Y, so it maps to the same scaling about the eye in rendered space, and each vertex stays on its own view ray. A 24-bit depth buffer with near = 1 m (Camera.cpp:26-27) resolves about d²/2²⁴ m at distance d. A pull of 0.004·d beats that out to d ≈ 0.004·2²⁴ ≈ 67 km. The largest terrain is 50 km (LocationDialog.cpp:177), and its initial radius is 40 km (ViewportWidget.cpp:144, 324), where the pull is 160 m against a 95 m step.
- **Eye position.** `Camera::eye()` is a new public member wrapping `camPos` (Camera.cpp:7-15). The viewport passes `eye / vec3(1, kVertScale, 1)`, so `kVertScale` stays private.
- **Dash period.** `14 px × metresPerPixel`, the same formula as `updateMapFurniture` (ViewportWidget.cpp:290-292), floored at 1 m.

**Passes.** Blend on, `glDepthMask(GL_FALSE)`, depth test on.
1. Halo: for each visible slot, four passes at pixel offsets (±1, 0) and (0, ±1) with `uHalo = 1`.
2. Core: one pass per visible slot.

Restore `glDepthMask(GL_TRUE)` and disable blend. No `glLineWidth` above 1 is relied on, since widths above 1 are not guaranteed in a Core context (main.cpp:7-11 requests no MSAA).

**Paint order.** In `paintGL`, between `m_losRay.drawProbe` (ViewportWidget.cpp:202) and `drawAntennaMarkers` (203). That is after the coverage drape, which blends without writing depth, and before markers, which draw with depth test off.

**Visibility.**
- **Committed outlines** draw when the existing "vegetation" LayerStack entry is visible (read each frame from `m_layers`; treated as visible if there is no stack), or whenever the tool is active.
- **Selection and footprint** draw only while the tool is active.
- **Preview** draws only while a gesture has been dragged.
- **No LayerStack id is added**, so guard:374 and test_layers' `size() == 10` hold. `isLayerVisible` is unchanged, so the data-gated guard check (guard:385-393) holds.

**Lifecycle (ViewportWidget).**
- **CPU buffers:** `m_vegCommitted`, `m_vegSelection` and `m_vegPreview`, each with a dirty flag.
- `refreshVegetationAreas()`: cancel the gesture, clamp the mirrored selection, rebuild committed and selection, mark dirty, `update()`.
- `setVegetationSelection(i)`: rebuild selection only.
- `paintGL`: uploads dirty buffers while its context is current. No GL work happens in any mouse handler.
- `initializeGL`: `m_vegetationOverlay.build()` after `m_fresnel.build()` (132), then rebuild the CPU buffers and mark all dirty.
- The `aboutToBeDestroyed` lambda (91-104): `m_vegetationOverlay.destroy()`, reset the gesture.
- `reloadTerrain` (295-334): destroy and build next to the other overlays, cancel the gesture, rebuild all CPU buffers. Draped heights depend on the terrain.

**Grips.** `antennaAtScreenPosition` (393-430) is rewritten on `ScreenProjection::toScreen(renderMvp, w, h, world, outPx)` (new `src/render/ScreenProjection.{h,cpp}`). The ±1.05 NDC margin, `w <= 0` rejection and z range are kept, so antenna picking is identical. Grip projection uses the same function.

### Canopy wash

The wash is still `CanopyShade::render` composited into the basemap through `applyRasterLayers`, keyed on `canopyRevision` (ViewportWidget.cpp:697-705).

**Deferred.** `applyVegetationState` only calls `scheduleVegetationWashRefresh()`, which restarts `m_vegetationWashTimer` (single-shot, 120 ms, objectName `VegetationWashTimer`). Its timeout calls `flushVegetationWash()`:
- during a solve or terrain load, it restarts the timer;
- otherwise it calls `m_viewport->refreshLayers()`.

`onExportPng` stops the timer and flushes before `grab()` (MainWindow.cpp:2280).

**Re-entrancy guard in `applyRasterLayers`.** The existing body moves to `applyRasterLayersOnce(bool force)`. The wrapper is:

```cpp
if (m_rasterBuildActive) { m_rasterBuildPending = true; m_rasterBuildForce |= force; return; }
m_rasterBuildActive = true;
const auto done = qScopeGuard([this] { m_rasterBuildActive = false; });
do {
    applyRasterLayersOnce(force);
    force = std::exchange(m_rasterBuildForce, false);
} while (std::exchange(m_rasterBuildPending, false));
```

A call arriving from a nested tile or Google loop (a wash timer, a layer toggle) is coalesced into one follow-up pass instead of destroying the basemap mid-build. The GL context is already re-asserted before upload (BasemapOverlay.cpp comment at the `m_prepareOpenGL` call).

`applyRasterLayersOnce` sets `m_appliedCanopyRevision = m_engine->canopyRevision()` whenever it gets past the `isValid()` check. `vegetationWashUpToDate()` compares the two.

**Stated mismatches**, in comments and README:
- The wash is stretched edge to edge and filtered, so it is out of register by up to half a sample.
- The raster rounds outward.
- Clearings show in the wash only as missing green (CanopyShade.h:13-19). The outline is the reliable indicator.
- Procedural terrain has no wash unless Hillshade is on (BasemapOverlay.cpp `loadTextureForTerrain`).

## Undo, dirty state and invalidation

### Command

`src/ui/SnapshotCommand.h` (new, header-only): `template <typename State> class SnapshotCommand final : public QUndoCommand`. It is the body of `SourceStateCommand` (MainWindow.cpp:65-109) moved verbatim and parameterised:
- before/after `State` and selection before/after;
- a description, `mergeId` and a timestamp;
- an `Apply` `std::function`;
- `mergeWith` uses `dynamic_cast<const SnapshotCommand<State>*>` with the same `mergeId >= 0` within 900 ms.

In MainWindow.cpp's anonymous namespace:

```cpp
using SourceStateCommand = SnapshotCommand<std::vector<Antenna>>;
using VegetationStateCommand = SnapshotCommand<std::vector<VegetationArea>>;
```

Source behaviour is unchanged. Different instantiations can never merge. Vegetation commands always use `mergeId -1`: each change is one discrete commit (gesture release, dialog OK, delete), so index shifts can never merge edits across areas.

### Push

`pushVegetationChange(std::vector<VegetationArea> after, int selectedAfter, const QString& description)` mirrors `pushSourceChange` (1734-1748):
- During a solve or terrain load, set status "  Wait for the current operation to finish before changing vegetation" and return. This is unreachable through the modal dialogs, and checked defensively like `onLoadCanopy` (870).
- With no stack, apply directly.
- Otherwise push `VegetationStateCommand(m_engine.vegetation(), m_selectedVegetationIndex, after, selectedAfter, description, -1, apply)`.

There is one `QUndoStack` (MainWindow.cpp:125). The guard relies on this (guard:141, 522), and mixed history then undoes in true time order.

### Apply

`applyVegetationState(const std::vector<VegetationArea>& areas, int selected)` serves draw, reshape, edit, delete, undo and redo:

```cpp
const auto revisionBefore = m_engine.canopyRevision();
m_engine.setVegetation(areas);                       // skips rebuild if footprints unchanged
if (m_engine.canopyRevision() != revisionBefore) {   // the raster the solver reads changed
    m_engine.setFieldGrid({});
    m_solvedSignature.clear();
    invalidateScenarioComparisonCurrent();           // keeps the baseline
    invalidateCalibrationCurrent();
    if (m_pointPanel) m_pointPanel->clear();
    if (m_pointDock) m_pointDock->hide();
    if (m_viewport) {
        m_viewport->clearAnalysisPoint();
        m_viewport->refreshOverlays();               // drops the stale heatmap texture (HeatmapOverlay.cpp:80-83, 168)
    }
    scheduleVegetationWashRefresh();                 // never refreshLayers() here
}
const int count = static_cast<int>(m_engine.vegetation().size());
m_selectedVegetationIndex =
    (m_viewport && m_viewport->vegetationDrawMode() && selected >= 0 && selected < count) ? selected : -1;
if (m_viewport) {
    m_viewport->setVegetationSelection(m_selectedVegetationIndex);
    m_viewport->refreshVegetationAreas();            // also cancels any in-progress gesture
}
refreshVegetationActions();
refreshCanopyActions();
Q_ASSERT(!resultsAreStale());                        // gives the private invariant check a caller
```

**Why invalidation is keyed on the rebuild.** `SolveSignature` hashes `canopyHeightDm` and `canopyCoverPct`. After the `sameFootprint` skip, a rebuild happens exactly when the grid may have changed. A rename keeps a valid solve on screen. A clearing over bare ground still clears the result, which is conservative.

**No `markScenarioDirty`**, as in `applySourceState`. Dirtiness comes from `!m_undoStack->isClean()` (MainWindow.cpp:1337-1339), so undoing back to the save point clears the title marker, the behaviour guard:140-147 pins for sources. A rename pushes a command, so it still dirties the scenario and reaches autosave.

**No `refreshBottomBar`:** it is antenna-only (MainWindow.cpp:1792-1798).

### Selection

Selection is view state, not undoable by itself, like antenna selection. Each command snapshots it, so undo and redo restore it. It is reset to -1 when the tool turns off, in `applyScenarioDoc`, and whenever it is out of range after an apply. It is index-based, which is sound only because every apply, load and tool exit re-clamps it and cancels gestures. This is written as a comment on `m_selectedVegetationIndex`.

### Autosave and recovery

No new code. `currentScenarioDoc` copies `m_engine.vegetation()` (MainWindow.cpp:1925), and `autosaveTick` writes when the stack is not clean (1944-1949). Previews live only in the viewport, so an autosave mid-drag writes committed state. `offerRecovery` goes through `applyScenarioDoc`, which resets the tool and selection.

### Solve interplay

`onSolve` copies `m_engine`, which holds only committed areas, behind a WindowModal dialog (1508-1563). Opening that dialog moves focus off the viewport, so `focusOutEvent` cancels any gesture.

## Persistence

**Save** (`ScenarioIO.cpp:46-148`):
- `j["version"] = 6` (informational; old loaders never read it).
- If any area is a clearing, also write `j["minReaderVersion"] = 6` and `j["requires"] = "Vegetation clearings: open with Ridgeline scenario format 6 or newer"`. The second key exists for a person reading the JSON after an old build's parse error.
- Each vegetation entry: `{"kind": "stand"|"clearing", "minX", "minZ", "maxX", "maxZ", "name", "canopyHeightM": <number for a stand> | null for a clearing}`.
- A comment at the null explains the fail-safe:
  - writing 0 would make older builds silently drop the clearing, keep the imported forest, and erase the clearing on their next save or autosave;
  - writing a positive height would plant a stand.
- `constexpr int kScenarioReaderVersion = 6;` goes in `ScenarioIO.h`.

**Load** (`ScenarioIO.cpp:171-194`, inside the existing try):
1. First statement: `const int minReader = j.value("minReaderVersion", 0);`. If `minReader > kScenarioReaderVersion`, throw `std::runtime_error("This scenario needs a newer version of Ridgeline (scenario format " + N + "); this build reads format 6 and earlier")`. A `runtime_error` passes through the `json::exception` catch (307) unchanged. Open (2062-2073) and recovery (1988-1993) already report `std::exception` text.
2. For each vegetation entry:
   - `kind = entry.value("kind", std::string{"stand"})`. If `!kindFromString`, throw the unknown-kind `runtime_error`.
   - Bounds as today.
   - A stand reads `canopyHeightM` with default 0, so a missing key is still dropped as invalid. A clearing sets height 0 without reading the key.
   - Push if `valid()`.

`ScenarioIO.cpp` still uses only header-inline VegetationAreas code.

**RecoveryStore** uses `ScenarioIO::save`/`load` (RecoveryStore.cpp:32-49), so recovery copies behave identically. No change.

**Not persisted:** tool state, selection and preview. There are no lat/lon keys; world metres stay authoritative because `applyScenarioDoc` rebuilds the file's terrain before `setVegetation` (2115, 2152).

## Ordered implementation steps with exact files and functions

Each step builds and passes `ctest` on its own; each can be committed separately.

**0. Design spec.**
- New `docs/design/specs/2026-09-13-vegetation-editing-design.md`, containing this document. Follows the convention of `2026-09-11-canopy-data-ingestion.md`.
- `docs/ROADMAP.md` is not touched.

**1. Sim: kinds, clearings, footprint, carry.**
- `src/sim/VegetationAreas.h`: `VegetationKind`, appended `kind`, inline `valid()` with finite and cap checks, inline `kindToString`/`kindFromString`, declarations of `Footprint footprint`, `sameFootprint`, `countOutside`, `ImportedUnder importedHeightUnder`, and the comments.
- `src/sim/VegetationAreas.cpp`: implement `footprint` (double-space clamp, outward rule, corrected comment); rewrite `apply` on `footprint` with the claim buffer; implement `sameFootprint`, `countOutside`, `importedHeightUnder`.
- `src/sim/SimEngine.h/.cpp`: the `setVegetation` skip; area corner carry in `setTerrain`; `sampleImportedCanopy`.
- `tests/test_canopy.cpp`: add `testClearings`, `testFootprintMatchesRaster` and `testSameFootprintAndImportedMedian`, and register them in `main()` (844).
- `tests/test_geospatial_antennas.cpp`: add `test_drawn_vegetation_keeps_its_ground`, `test_drawn_vegetation_verbatim_without_georeference` and `test_vegetation_rename_does_not_rebuild_canopy`, and register them in `main()` (270).
- `tests/test_layers.cpp`: add `testCanopyShadeShowsClearingAsHole` and register it.

**2. Persistence.**
- `src/data/ScenarioIO.h`: `kScenarioReaderVersion`.
- `src/data/ScenarioIO.cpp`: `save` (version 6, `kind`, null height, `minReaderVersion`/`requires`) and `load` (ceiling, kind parsing, clearing height).
- `tests/test_scenario_save.cpp`: extend the vegetation block (189-224).

**3. Pure helpers and the undo template.**
- New `src/render/TerrainTriangulation.h`. Use `appendCellIndices` in `TerrainMesh::build` (TerrainMesh.cpp:25-33), `BasemapOverlay::build` (286-293) and `HeatmapOverlay::build` (42-51).
- New `src/map/VegetationGeometry.{h,cpp}`: `Rect`, `boundsOf`, `fromCorners`, `coversACell`, `pick`, `Grip`, `hitTest`, `reshape`, `meshHeightAtWorld`, `cornersOnMesh`, `LineVertex`, `styleFor`, `appendOutline`.
- New `src/render/ScreenProjection.{h,cpp}`: `toScreen`.
- `src/render/TerrainRaycast.{h,cpp}`: add `onHorizontalPlane(viewProjection, w, h, sx, sy, verticalScale, planeYUnscaled, glm::vec2& outXZ)`, sharing the unprojection code. It returns false if `t <= 0` or the hit is beyond the far point.
- New `src/ui/SnapshotCommand.h`.
- `CMakeLists.txt`:
  - add `src/render/ScreenProjection.cpp` to `ridgeline_location_picking_tests` (254-258);
  - new target `ridgeline_vegetation_edit_tests`: `tests/test_vegetation_edit.cpp src/map/VegetationGeometry.cpp src/sim/VegetationAreas.cpp src/sim/TerrainModel.cpp src/data/TerrainLoader.cpp`, include `src`, link `Qt6::Gui glm::glm nlohmann_json::nlohmann_json`, `add_test(NAME vegetation_edit_tests ...)`.
- `tests/test_location_and_picking.cpp`: `testScreenProjectionRoundTrip`, `testAnchorPlaneFallback`, registered in `main()` (163).
- New `tests/test_vegetation_edit.cpp`.

**4. Renderer, with no interaction yet.**
- New `assets/shaders/outline.vert` and `outline.frag`.
- New `src/render/overlays/VegetationAreaOverlay.{h,cpp}`.
- `src/render/Camera.h/.cpp`: `glm::vec3 eye() const`.
- `src/render/overlays/HeatmapOverlay.h`: `bool hasField() const { return m_texW > 0; }`.
- `src/ui/ViewportWidget.h/.cpp`:
  - overlay member and CPU buffers;
  - `refreshVegetationAreas`, `setVegetationSelection`, `vegetationSelection`;
  - overlay lifecycle in `initializeGL`, the `aboutToBeDestroyed` lambda and `reloadTerrain`;
  - `drawVegetationAreas()`, called from `paintGL` between 202 and 203;
  - `applyRasterLayers` split into a guarded wrapper and `applyRasterLayersOnce`, plus `m_appliedCanopyRevision`;
  - `coverageFieldDrawn`, `vegetationWashUpToDate`, `vegetationOutlineVertexCount`;
  - `antennaAtScreenPosition` on `ScreenProjection`.

**5. Viewport interaction.**
- `src/ui/ViewportWidget.h/.cpp`:
  - `setVegetationDrawMode`, `vegetationDrawMode`, `vegetationDrawKind`, `cancelVegetationGesture`, `vegetationGestureActive`, the signals, `VegetationGesture` and `m_vegSwallowUntilRelease`;
  - branches in `mousePressEvent`, `mouseMoveEvent` (including the hover cursor and right-drag orbit), `mouseReleaseEvent`, `wheelEvent` and `keyPressEvent`;
  - new `focusOutEvent`;
  - `updateVegetationPreview` and `resolveVegetationPoint`;
  - one-line exclusivity additions in `setMeasureMode` and `setPlacementMode`.

**6. MainWindow, dialog and help.**
- New `src/ui/VegetationAreaDialog.{h,cpp}`.
- `src/ui/MainWindow.h`:
  - private slots `onVegetationDrawModeChanged(bool,int)`, `onVegetationAreaDrawn(float,float,float,float)`, `onVegetationAreaReshaped(int,float,float,float,float,bool)`, `onVegetationAreaClicked(int)`, `onDeleteVegetationArea()`, `onEditVegetationArea()`, `onDeleteOffTerrainVegetation()`;
  - private `applyVegetationState`, `pushVegetationChange`, `refreshVegetationActions`, `scheduleVegetationWashRefresh`, `flushVegetationWash`, `vegetationEffectSummary`, `defaultStandHeightM`, `nextVegetationName`;
  - members `m_selectedVegetationIndex = -1`, the five action pointers, `QTimer* m_vegetationWashTimer`, `float m_lastStandHeightM = 15.0f`.
- `src/ui/MainWindow.cpp`:
  - replace `SourceStateCommand` with the aliases;
  - create the actions in `VegetationMenu`;
  - wire them near 422-441;
  - create the wash timer near 537;
  - connect viewport signals near 548-559;
  - initial `refreshVegetationActions()` near 523;
  - `refreshCanopyActions` appends the drawn count;
  - `onLoadCanopy` text;
  - `onExportPng` flush;
  - tool, timer and selection resets plus status suffixes in `loadGlobalLocation`, `loadTerrain` and `applyScenarioDoc`.
- `src/ui/ShortcutsDialog.cpp`: rows as listed.
- `CMakeLists.txt`: add `src/ui/VegetationAreaDialog.cpp` to `ridgeline_ui_interaction_tests` (264-282).
- `tests/test_ui_interactions.cpp`: `#include "ui/VegetationAreaDialog.h"`, `testVegetationAreaDialog()`, registered in `main()` (940-951).

**7. GUI tests.**
- `CMakeLists.txt`, inside `if(RIDGELINE_GUI_TESTS)` after the existing target (340): `ridgeline_vegetation_gui_tests` from `tests/test_vegetation_gui.cpp ${RIDGELINE_APP_SOURCES}`, with the same include, definition, link, GDAL, `add_dependencies(... ridgeline)` and `add_test(NAME vegetation_gui_tests ...)` as `ridgeline_main_window_tests`. The existing target is untouched.
- New `tests/test_vegetation_gui.cpp`. Its `main()` preamble copies guard:1627-1650: surface format, Fusion, palette, org and app name, temporary `RIDGELINE_STATE_DIR`.

**8. Documentation.**
- `README.md`: rewrite the "Drawn by hand" paragraph (199-202) to cover:
  - the Draw Stands and Draw Clearings tool and its gestures;
  - that clearings beat stands;
  - the outward rounding and the footprint rectangle;
  - the default height rule;
  - carrying areas by latitude and longitude across georeferenced terrain changes;
  - that older builds refuse scenarios containing clearings.

## Test plan

### Headless

**`tests/test_canopy.cpp` (canopy_tests)**
- **`testClearings`**
  - On an 8×8, 10 m flat terrain with an imported base of 180 dm and 40% cover, a clearing `{0,0,20,20}` leaves height 0 at the samples inside and the import (180 dm, 40%) at the far corner.
  - A clearing and an overlapping stand give byte-identical height and cover grids in both list orders, with 0 in the overlap.
  - Stands that overlap only each other still take the tallest.
  - Clearings alone with no import give `hasVegetation() == false` and both grids empty.
  - A clearing covering the whole import gives `hasVegetation() == false`.
  - A clearing carrying `canopyHeightM = 15` still clears.
  - `valid()` is true for a zero-height clearing and false for: a zero-height stand, a NaN-height stand, a NaN bound, an infinite bound, a 1e30 bound, an inverted clearing.
  - `SolveSignature::of` changes when a clearing removes imported canopy, and is equal to the no-area signature when the clearing lies only over bare base samples.
- **`testFootprintMatchesRaster`**
  - On a 64×64, 10 m grid, for rectangles `[101,199]`, `[100,200]`, `[101,102]`, `[-500,150]`, `[5000,6000]` and `[-9e6,9e6]`: the set of samples `apply` paints equals exactly `footprint()`'s `x0..x1 × z0..z1`.
  - `[101,199]` gives 10..20 and `[101,102]` gives 10..11. This pins the kept outward rounding.
  - The off-terrain rectangle is empty. The ±9e6 rectangle paints the whole grid without undefined behaviour.
  - Footprint world bounds equal `[(x0-0.5)·10, (x1+0.5)·10]` clipped to the extent.
  - `countOutside` counts only the off-terrain area.
- **`testSameFootprintAndImportedMedian`**
  - `sameFootprint` ignores names; detects kind, bound and height-in-decimetre changes; treats 15.00 and 15.04 m as equal.
  - `importedHeightUnder`: a rectangle wholly over an 18 m import gives `vegetated == samples` and median 18.0. A rectangle half over it gives a fraction of about 0.5.

**`tests/test_rf_models.cpp` (rf_model_tests)**
- **`testVegetationAreasRasterise`**, unchanged. It must still pass, including the zero-height stand being ignored (789) and the clamp and overlap checks. This is the regression guard for the default kind and the unchanged rounding.

**`tests/test_layers.cpp` (layer_tests)**
- **`testCanopyShadeShowsClearingAsHole`**: over an imported canopy grid, `CanopyShade::render` has alpha 0 at a sample inside a clearing and alpha above 0 outside it. `LayerStack` still has 10 layers (existing assertions untouched).

**`tests/test_scenario_save.cpp` (scenario_save_tests)**
- A stand plus a clearing round-trip kind, name and bounds; the clearing comes back with height 0.
- Parsing the saved file with nlohmann gives:
  - `version == 6`, `minReaderVersion == 6`, `requires` present;
  - `vegetation[0]["kind"] == "stand"` with a numeric `canopyHeightM`;
  - `vegetation[1]["kind"] == "clearing"` with `canopyHeightM.is_null()`.
- Old-reader tripwire: `raw["vegetation"][1].value("canopyHeightM", 0.0f)` throws `nlohmann::json::type_error`.
- A stands-only file has neither `minReaderVersion` nor `requires`, and `value("canopyHeightM", 0.0f)` succeeds on every entry.
- A legacy entry without `kind` loads as a Stand with its height.
- `kind: "polygon"` makes `load` throw `std::runtime_error` whose message contains "polygon" and not "Scenario parse error".
- `minReaderVersion: 7` makes `load` throw with a message containing "newer".
- An inverted clearing, and an entry with `"minX": 1e39`, are dropped.
- `RecoveryStore::write` then `read` (with `RIDGELINE_STATE_DIR` set to the temp directory) preserves a clearing.

**`tests/test_geospatial_antennas.cpp` (geospatial_antenna_tests)**
- **`test_drawn_vegetation_keeps_its_ground`**
  - Set up: `makeCustomGeoTerrain(101, 0, 35.0, -80.0, 0.1)`, then `setVegetation({stand})`, record its corner latitudes and longitudes, then `setTerrain(makeCustomGeoTerrain(121, 0, 34.98, -80.03, 0.14))`.
  - The corners map back to the same latitude/longitude within 1e-6°.
  - The canopy sample at `latLonToWorld(centre)` is above 0.
  - The sample at the stand's old world centre is 0, provided it lies outside the moved stand.
- **`test_drawn_vegetation_verbatim_without_georeference`**: procedural-to-procedural and georeferenced-to-procedural switches keep bounds bit-identical.
- **`test_vegetation_rename_does_not_rebuild_canopy`**: renaming leaves `canopyRevision()` unchanged and updates `vegetation()[0].name`. A height change from 15 to 16 m, and a kind flip, each increment it.

**`tests/test_location_and_picking.cpp` (location_picking_tests)**
- **`testScreenProjectionRoundTrip`**: on `makeSlopedTerrain`, `ScreenProjection::toScreen(vp·scale(1, 2.5, 1), ...)` of a terrain point, then `TerrainRaycast::fromScreen` at `(px - 0.5)`, returns the point within 0.25 m, at views of about 30° and 89°. A point behind the camera returns false.
- **`testAnchorPlaneFallback`**: for a pixel looking past the terrain edge, `fromScreen` is false and `onHorizontalPlane` returns a finite XZ beyond the extent. A pixel above the horizon returns false.

**`tests/test_vegetation_edit.cpp` (new, vegetation_edit_tests)**
- **`testTriangulationMatchesIndices`**: for a 2×2 grid, the triangles from `appendCellIndices` interpolated barycentrically at random `(fx, fz)` equal `heightInCell`.
- **`testMeshHeightFollowsDiagonal`**: on a twisted cell (h00 = h10 = h01 = 0, h11 = 10), `meshHeightAtWorld` is 0 at the centre and 5 at (0.75, 0.75), and equals `heightAt` at the samples.
- **`testOutlineLiesOnMesh`**: on `makeHills` terrain, every `appendOutline` vertex's y equals `meshHeightAtWorld(x, z)`, and every segment midpoint does too, within 1e-3 m.
- **`testOutlineClippingAndStyle`**:
  - the vertex count is even;
  - a partly off-terrain rectangle has all x and z within `[0, worldSize]`, and a fully off one emits nothing;
  - `along` is 0 at the first vertex, non-decreasing along each edge, and its maximum equals the perimeter for an on-terrain rectangle;
  - clearings are marked dashed and stands solid;
  - footprint and preview roles carry their alpha.
- **`testFromCornersAndCoversACell`**: all four corner orders give the same normalised rectangle, clamped to the extent. `coversACell` rejects rectangles below one cell on either axis.
- **`testPickSmallest`**: a nested rectangle beats its container; a tie goes to the later index; invalid areas are skipped; outside every area gives -1.
- **`testGripHitTest`**:
  - a corner within the radius gives that corner; inside gives Body; a corner beats Body;
  - the radius is capped at a third of the shortest projected edge;
  - a corner flagged invisible is ignored; outside gives None.
- **`testReshape`**:
  - Move keeps the size and stays inside the extent when it started inside;
  - a corner resize keeps the opposite corner fixed and preserves the grab offset;
  - dragging past the opposite corner re-normalises.
- **`testSnapshotCommandMerging`**:
  - two `SnapshotCommand<std::vector<Antenna>>` with id 1000 pushed together merge;
  - a `SnapshotCommand<std::vector<VegetationArea>>` with the same id does not merge with them;
  - id -1 never merges;
  - interleaved source and vegetation pushes undo in reverse order, restoring both recorded states, and redo reproduces them.

**`tests/test_ui_interactions.cpp` (ui_interaction_tests, offscreen)**
- **`testVegetationAreaDialog`**:
  - the dialog and `VegetationAreaName`, `VegetationAreaKind`, `VegetationAreaHeight` and `VegetationAreaEffect` exist;
  - the height maximum is `kMaxCanopyHeightM` and the minimum is 0.5;
  - selecting Clearing disables the height, and `area()` returns kind Clearing, height 0 and the original bounds;
  - switching back to Stand restores the previous height;
  - the edited name round-trips, and an empty name falls back to the original.

### UI (`tests/test_vegetation_gui.cpp`, needs `RIDGELINE_GUI_TESTS` and a display)

- **`testGuardDependenciesStillHold`**:
  - `findChildren<QUndoStack*>().size() == 1`;
  - `findChild<QListWidget*>()->objectName() == "SourceList"`;
  - `findChild<QDoubleSpinBox*>()` has a `PropertiesPanel` ancestor;
  - the first `QPushButton` whose text contains "Remove" has a `ScenarioTree` ancestor;
  - `LayerList` has 10 rows;
  - each of the guard's five action substrings matches exactly one `QAction`;
  - the five new actions exist with their objectNames, and `findChild<VegetationAreaDialog*>()` is null.
- **`testDrawToolExclusivity`**:
  - Triggering Stands turns `vegetationDrawMode()` on with kind Stand.
  - Triggering Clearings switches the kind and unchecks Stands.
  - `MeasureButton->setChecked(true)` turns the tool off and unchecks both actions.
  - Re-enable Stands, then `invokeMethod(window, "onAddAntenna")`: the tool turns off.
  - Enabling Stands during that placement emits `placementCancelled` exactly once. A later click on terrain adds no source.
- **`testDragDrawsOneStandAndInvalidates`**:
  - Setup: set quality to preview, click Run coverage, settle. The legend is visible and `coverageFieldDrawn()` is true. Enable Stands and call `topView()`. Record `NorthIndicator::cameraAzimuth()`.
  - Press at centre −(60, 45), send six move events with the left button to centre +(60, 45), release.
  - The undo count rises by exactly 1 and `undoText() == "Draw stand"`; `isWindowModified()`.
  - The legend is hidden and `coverageFieldDrawn()` is false.
  - `vegetationOutlineVertexCount() > 0`.
  - `DeleteVegetationAreaAction` is enabled and the `SourceList` count is unchanged.
  - The azimuth is unchanged, so the drag did not orbit.
  - `vegetationWashUpToDate()` is false immediately and true after `settle(400)`.
  - Status contains "assumed".
- **`testClickSelectsAndKeysEditAndDelete`**:
  - Escape leaves Delete disabled and the tool on.
  - A click inside the stand enables Delete.
  - `Key_Return` opens a `VegetationAreaDialog` (found through `activeModalWidget` by a `QTimer`), which is cancelled: no new command.
  - `Key_Delete` gives `undoText() == "Delete vegetation area"` and outline count 0.
  - Undo restores a count above 0.
- **`testClearingRemovesStand`**: switch to Clearings and drag a larger rectangle over the stand. `undoText() == "Draw clearing"`. After settle, `isLayerVisible("vegetation")` is false; this is documented in the test as a proxy for `terrain().hasVegetation()`.
- **`testCancelledGestureCommitsNothing`**, three variants that each start with press plus moves (`vegetationGestureActive()` true):
  - Escape;
  - `history->undo()`, where the count drops by exactly 1;
  - a `QFocusEvent(FocusOut)` sent to the viewport.
  
  In each variant: `vegetationGestureActive()` becomes false; further left-button moves leave the azimuth unchanged; the release adds no command and emits no `terrainInspected` and no `placementCancelled`; the tool stays on. Finally, a right-drag in the tool changes the azimuth.
- **`testUndoRedoReturnsToSavedState`**: undo every command, then `!isWindowModified()`. Redo everything, then `isWindowModified()`.
- **`testRenameKeepsResultHeightDiscardsIt`**:
  - After a new solve, Enter and a dialog rename (name only) give `undoText() == "Edit vegetation area"` with the legend still visible and `coverageFieldDrawn()` true.
  - Enter and a height change hide the legend.
- **`testAutosaveWritesCommittedAreasOnly`**:
  - Draw one stand and one clearing. Set `AutosaveTimer`'s interval to 20 ms.
  - Start a Move of the clearing without releasing, then `settle(200)`. `RecoveryStore::read().vegetation` has a Stand with height above 0 and a Clearing with height 0, at their committed bounds.
  - Release, `settle(200)`: the recovery copy has the moved bounds.
- **`testSubCellAreaIsRefused`**: `invokeMethod(window, "onVegetationAreaDrawn", 100.0f, 100.0f, 105.0f, 105.0f)` with the tool on adds no command, and status contains "smaller than one".
- **Unchanged guard.** With `RIDGELINE_GUI_TESTS=ON`, `main_window_tests` must still pass. That proves the `setMeasureMode`/`setPlacementMode` additions, the `SnapshotCommand` template, the `antennaAtScreenPosition` refactor and the `applyRasterLayers` guard did not change behaviour the locked file pins.

## Known limits to document

- **Rectangles only**, axis-aligned in terrain world metres. At the default 45° azimuth they look rotated on screen, although the outline is exact.
- **Solver footprint larger than the outline.** The painted footprint rounds outward: painted samples lie up to one sample beyond the outline on each side, and the ground attributed to them up to one and a half; a clearing over-clears by as much. The selection's dim footprint rectangle and the "solves as" text show it. The rule is kept because saved scenarios, including persisted calibration corrections, were produced with it.
- **Clearings beat stands, whatever the order.** A copse standing inside a clearing needs the clearing split around it.
- **Drawn stands** are complete cover (100%). A new stand's height is the imported median where at least half its samples carry imported canopy. Otherwise it is the last height set this session (15 m at first), which the status line names as assumed.
- **Carrying areas across terrain changes:**
  - by latitude and longitude only when both terrains are georeferenced; otherwise verbatim metres;
  - size in metres can change slightly between terrains linearised at different centre latitudes;
  - areas carried off the terrain are invisible and unselectable, but are saved, counted in the load status and the Vegetation menu, and removable with "Delete Areas Outside This Terrain".
- **Older builds** refuse any scenario or recovery copy containing a clearing, with a generic "Scenario parse error". An older build started after a newer session reports "Could not recover". If edited there, its autosave can overwrite the newer recovery copy.
- **The green wash:**
  - updates about 120 ms after the outline;
  - is out of register by up to half a sample and softened by filtering;
  - is absent on procedural terrain unless Hillshade is on;
  - shows a clearing only as missing green.
- **Every committed change rebuilds the basemap**: cached tile reads, network for misses, and a Google session request plus a metadata request per rebuild when a key is configured. It also rasterises the canopy, including the imported resample, on the UI thread.
- **Outline rendering:**
  - a 1 px core with a 1 px dark halo, no MSAA;
  - hidden behind ridges (no x-ray);
  - can show through terrain features closer than 0.4% of the viewing distance;
  - below about 15° camera elevation, distant segments can drop under the imagery, where the basemap's slope-scaled polygon offset outgrows the pull.
- **Gestures:**
  - inside the tool, a left drag over terrain draws instead of orbiting; right-drag, arrow keys and wheel still navigate;
  - dragging inside the selection moves it, so drawing a nested stand needs a deselect first;
  - corner grips ignore occlusion;
  - picking at grazing angles is coarse, and a release resolved only from the last good point commits nothing;
  - a right or middle press mid-drag cancels the gesture.
- **Delete, Enter and Escape** need viewport focus, as Escape does today. The menu actions are the fallback.
- **Undo history** is cleared on every terrain, location or scenario load, as for sources.
- **Selection is index-based.** Any future caller of `SimEngine::setVegetation` must also call `ViewportWidget::refreshVegetationAreas` and reset the selection.
- **Pre-existing hazard, partly mitigated:** `reloadTerrain` or a scenario open dispatched inside a nested tile loop still destroys the basemap under an in-flight build. The new guard now forces a clean rebuild afterwards.
- **GUI tests** need a display and compile the app sources a second time.

## Explicitly out of scope

- Polygon, rotated, strip or freehand areas, and a point-in-polygon rasteriser.
- Changing the rasteriser to sample-centre inclusion, with a per-file rule marker. Recommended as its own slice.
- A vegetation list or dock panel, bulk rename, table editing, reordering.
- Per-area crown cover, seasonal state, and a regrowth height for clearings.
- Stored latitude/longitude keys for areas, and carrying areas between procedural and georeferenced terrain.
- A toolbar button, single-letter shortcuts, a HUD hint label, map labels, a draped fill, handle glyphs.
- A recomposite-only `BasemapOverlay` path that re-tints canopy without rebuilding the mesh or reading tiles.
- Wide lines by screen-space quad extrusion, and MSAA.
- Snapping top view to north.
- Replacing the loose mode booleans with one enum. Fixing the pre-existing gaps where measure does not block antenna drag, and where measure cancels placement without emitting `placementCancelled`.
- Clearing the stale heatmap and point inspector in `installCanopy`/`onClearCanopy` (MainWindow.cpp:857-866, 959-966). This is the same class of bug this slice avoids, and needs a network-backed test.
- `restoreCanopyImport` ignoring the recorded `sourceId`/`retrievedUtc`, and the stale `m_canopy` summary after a location change.
- `ScenarioDoc` load warnings for skipped entries.
- PointAnalysis explanations attributing an outage to vegetation.
- Any edit to `tests/test_main_window_guard.cpp` or `docs/ROADMAP.md`.

## Review amendments

An adversarial review before merge changed these points of the design:

- **Bound cap 5e7 m, not 1e7 m.** A geo carry to the far side of the world moves an area about 2e7 m. Under a 1e7 m cap it became invalid, then uncounted and unselectable, and was dropped on the next save.
- **Footprint quotient in float.** `footprint()` divides in float, as the original rasteriser did, and widens to double only to clamp. Dividing in double moved some bounds typed as decimal multiples of the cell size by a sample.
- **Invalidate on grid change, not on rebuild.** `applyVegetationState` keeps results when a rebuild leaves `canopyHeightDm` and `canopyCoverPct` identical, as deleting off-terrain areas does. The map texture still follows every rebuild.
- **Untouched dialog.** A stand whose height the operator did not edit keeps its stored height, even outside the spin box's range.
- **Mouse state.** Only a left press with no other button held starts a gesture. Only a left press clears the swallow flag. Inside the tool, a buttonless move drops stale drag state.
- **Outline depth margin.** See the rendering note in the PR: the imagery drape's slope-scaled polygon offset was checked against the outline's fixed lift and pull at grazing angles.
