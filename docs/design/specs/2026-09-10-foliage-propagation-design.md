# Foliage propagation

Roadmap item 13. Design from an investigation whose every claim was put to an
adversarial reviewer told to default to refuting; 17 claims were refuted and are
reflected below. Where this document states something the sources do not
support, it says so.

## Why this is not "add a foliage model"

`assets/materials.json` already defines a `foliage` material with
frequency-dependent loss. It has never been used. Both solvers take a
`const MaterialLibrary&` and then call the *static* `defaultMaterial()` through
it, which is dry soil:

- `src/sim/FastSolver.cpp:38`, `src/sim/FullSolver.cpp:56`
- `MaterialLibrary::find()` is called nowhere in `src/` — only in tests

So `foliage`, `concrete`, `asphalt` and `water` are parsed at startup and
unreachable, and every solve applies dry-soil loss regardless of ground cover.
`SimEngine::loadMaterials` also swallows every exception
(`src/sim/SimEngine.cpp:134-139`), so a missing file silently degrades foliage
to ground loss with no diagnostic.

The missing capability is **spatial**: nothing records where vegetation is.

## The honesty problem, stated first

ITU-R P.833-10 contains no sanctioned terrestrial model for the geometry
Ridgeline actually has — extensive woodland mid-path with **both terminals
outside** it.

- §3.1 opens: *"Equation (1) does not apply for a radio path obstructed by a
  single vegetative obstruction where both terminals are outside the vegetative
  medium."* Equation (1) is the one-terminal-inside-woodland model.
- Equation (7)'s simple `γ·d` form sits under the heading *"At or below 1 GHz"*.
  §3.2 directs terrestrial paths above 1 GHz to a radiative-energy-transfer
  method for a single tree — excluding most bands a planner serves.

An exponentially-saturating model is a defensible engineering choice for this
geometry. It is **not** P.833-conformant, and the application must not claim it
is. This matches how Ridgeline already treats vertical datum and provider
attribution: state what is known, refuse what is not.

Two more corrections that constrain the maths:

- **`Am(f)` must not be extrapolated above ~2.2 GHz.** P.833's own `Am` data are
  non-monotonic (34.1 dB at 2118 MHz, 46 dB at 3605 MHz, 34.5 dB at 28 GHz), so
  a monotone `A1·f^ν` fit above 2.2 GHz is unsupported and comes out roughly 3×
  too pessimistic in the deep-foliage limit.
- **Table 1's `γ` entries mix polarisations** — 105.9 MHz is horizontal, the
  other four are slant. Interpolating one curve across them injects a
  polarisation discontinuity precisely where P.833 says polarisation matters
  most.

## What the current numbers get wrong

The shipped `foliage` table is wrong in two opposite directions at once:

- Its coefficients are roughly **1.8–2.2× below** P.833's measured specific
  attenuation between 106 and 2118 MHz.
- Because the form is unbounded linear, it **over-predicts past a few hundred
  metres**: about 96 dB at 1 km of woodland at 949 MHz, against a measured
  physical asymptote near 26.5 dB.

Real foliage loss saturates, because a path around or over the canopy eventually
wins. Any linear dB/m form is only defensible for shallow depths.

## Architecture

### Carrying vegetation

A canopy-height grid parallel to `heights` on `TerrainModel`:

```cpp
std::vector<std::uint8_t> canopyHeightDm;  // decimetres above ground; 0 = none
                                           // empty = no vegetation data at all
```

Empty by default, so every existing construction site and test helper compiles
unchanged. `validate()` gains one rule: if non-empty, the size must equal
`heights.size()`. At the practical 1024×1024 ceiling this is 1 MiB.

Decimetres in a byte covers 0–25.5 m, which spans essentially all canopy. A
height grid is the right primary input: an attenuation model needs **depth in
metres**, which only canopy height over the bare-earth model can produce. Cover
percent modulates the coefficient; land-cover class alone cannot produce depth
at all.

### Measuring depth along a path

`lineOfSightStatus` already walks the path and is called unconditionally on
every analytic path, so the depth comes out nearly free. It must gain an
**optional out-parameter**, not a changed return type: four call sites outside
the solver consume the enum directly — `FresnelOverlay.cpp:94`,
`LOSRayOverlay.cpp:41` and `:69`, `PointAnalysis.cpp:88` — and a struct return
breaks all of them.

Depth accumulates over samples where the ray passes below canopy top but above
bare ground, reusing the comparison already written at `TerrainModel.cpp:166`.

### Applying the loss

`RFModels::vegetationLossDb(depthM, freqMHz, ...)`, a saturating law:

```
Aev = Am(f) · [1 − exp(−d · γ(f) / Am(f))]
```

It goes **where `rainDb` already sits**. Rain is the existing precedent for an
excess-loss term that rides on both branches: it is computed outside the ITM
test at `FullSolver.cpp:95-96` and applied in the ITM path at line 106. Adding
foliage only to the analytic branch would make it silently inert in Engineering
mode beyond 1 km, where ITM takes over.

ITM itself models no vegetation: its API takes no land-cover input. It applies a
clutter factor in the diffraction region only, capped at 15 dB, derived from
bare-earth terrain roughness. Overlap is therefore real but bounded, and
additive application is reasonable.

**Canopy must not be added to the terrain profile handed to ITM.** `delta_h`
feeds both the diffraction clutter factor and, via
`LineOfSightLoss.cpp:58`, a weighting term that blends the two-ray result toward
diffraction — so inflating it with canopy corrupts the LOS region as well.

## Data sources — later slice, but decided now

Verified licences and access:

| Source | Gives | Coverage | Licence |
| --- | --- | --- | --- |
| LANDFIRE EVH + EVC | height + cover, 30 m | US only | CC0 public domain |
| Meta/WRI CHM v2 | height, 10 m | global | CC BY 4.0 |
| ESA WorldCover v200 | class, 10 m | global (2021 snapshot) | CC BY 4.0 |

Consequences that must be designed for, not discovered:

- **CC BY is a redistribution obligation.** An offline package bundling Meta or
  ESA data must carry the licence notice and required credit. The package format
  needs an attribution manifest before any CC BY layer ships. LANDFIRE's CC0
  avoids this, which is why it is the US default.
- **Meta's tiles are not slippy-map tiles.** They sit on a web-mercator quadkey
  grid but are large GeoTIFFs, so the existing XYZ transport does not simply
  consume them.
- **LANDFIRE EVH has a decode hole that biases populated areas to zero.** Values
  13, 14, 15, 17 (Developed-Upland Deciduous/Evergreen/Mixed Forest and
  Shrubland) and 100 (Sparse Vegetation Canopy) are vegetated pixels carrying no
  height. Treating "everything else" as non-vegetation deletes urban canopy
  exactly where links are. These need an explicit policy.
- **EVC bins start at 110, not 101**, with a hard 10% floor; below that there is
  only class 100.
- There is **no national USGS 3DEP canopy-height product**;
  `elevation.nationalmap.gov` serves bare-earth only.

## Scope

**This slice:**

1. Make the material library reachable, and make a missing/malformed
   `materials.json` loud instead of silent.
2. `canopyHeightDm` on `TerrainModel`, with validation and scenario persistence.
3. Path depth measurement via an out-parameter on `lineOfSightStatus`.
4. `RFModels::vegetationLossDb` with a saturating law and a frequency-dependent
   `γ`/`Am`, capped rather than extrapolated above 2.2 GHz.
5. Applied where `rainDb` is, so it reaches both solver branches.
6. Manual vegetation areas, so the feature is usable with no data source.
7. Labelling that states the model is an engineering extrapolation.

**Deferred:** LANDFIRE and Meta ingestion, the attribution manifest the CC BY
layers require, cover-percent modulation of `γ`, seasonal leaf state, and
polarisation dependence.

## Testing

Headless: the saturating law against P.833 Table 1 anchor points; saturation
(doubling depth must not double loss beyond the knee); the cap above 2.2 GHz;
depth measurement over a synthetic canopy with known geometry; validation
rejecting a mismatched canopy grid.

Native: a vegetated area changes coverage; the same scene with vegetation
cleared returns the previous result; the extrapolation notice is visible.

## Not established

No measured validation against field data — none of this is verified against
real RF measurements, and the application must not imply otherwise. Honest
accuracy for these models is roughly 5–10 dB RMS at UHF even when tuned to a
site.
