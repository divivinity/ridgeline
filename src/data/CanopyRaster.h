#pragma once
#include "sim/TerrainModel.h"

#include <cstdint>
#include <string>
#include <vector>

// Where a canopy layer came from. Ridgeline already refuses to display
// elevation whose provider it cannot name; vegetation is held to the same bar,
// because a canopy raster is a model with a vintage and a licence, not a
// measurement of today.
struct CanopyProvenance {
    std::string sourceId;        // stable key, e.g. "landfire-lf2024"
    std::string displayName;     // shown to the operator
    std::string productVintage;  // what the data describes, e.g. "LANDFIRE 2024"
    std::string licence;         // e.g. "Public domain (US Government work)"
    std::string attribution;     // credit line to carry into exports, may be empty
    std::string retrievedUtc;    // ISO-8601 UTC, when this copy was fetched
    double sourceResolutionM = 0.0;

    bool empty() const { return sourceId.empty(); }
};

// What the fetch actually produced, so the operator is told rather than left to
// assume. A request outside the provider's extent succeeds at the HTTP level
// and returns no-data everywhere; only a count can tell the difference.
struct CanopyCoverage {
    int samples        = 0;  // grid cells requested
    int withData       = 0;  // cells the provider answered for at all
    int vegetated      = 0;  // cells carrying canopy above zero
    int assignedHeight = 0;  // cells whose height was substituted, not measured

    double dataFraction() const {
        return samples > 0 ? static_cast<double>(withData) / samples : 0.0;
    }
    double vegetatedFraction() const {
        return samples > 0 ? static_cast<double>(vegetated) / samples : 0.0;
    }
    double assignedFraction() const {
        return vegetated > 0 ? static_cast<double>(assignedHeight) / vegetated : 0.0;
    }
};

// A canopy layer in its own geographic frame, north-up in WGS84.
//
// Stored geographically rather than on the terrain grid on purpose: terrain is
// reloaded at different resolutions, from different providers, over shifted
// footprints. A canopy raster anchored to latitude and longitude survives all
// of that and is resampled onto whatever grid is live, so the two always line
// up. Anchoring it to the grid instead would silently smear vegetation sideways
// the first time the terrain resolution changed.
struct CanopyRaster {
    int width  = 0;
    int height = 0;

    // Row 0 is the northernmost row, matching how raster providers hand over an
    // image; TerrainModel's row 0 is its southern edge, and sampleOnto flips.
    double northLatDeg = 0.0;
    double westLonDeg  = 0.0;
    double latStepDeg  = 0.0;   // positive degrees per row, southward
    double lonStepDeg  = 0.0;   // positive degrees per column, eastward

    std::vector<std::uint16_t> heightDm;   // canopy height above ground
    std::vector<std::uint8_t>  coverPct;   // crown cover 0-100; may be empty

    CanopyProvenance provenance;
    CanopyCoverage   coverage;

    bool empty() const { return heightDm.empty(); }
    // A raster is usable only if its geometry is consistent and it carries at
    // least one vegetated cell. An all-zero canopy is not vegetation data; it
    // is an area the provider does not cover, and installing it would claim
    // knowledge that there are no trees here.
    bool valid() const;

    double southLatDeg() const { return northLatDeg - height * latStepDeg; }
    double eastLonDeg()  const { return westLonDeg  + width  * lonStepDeg; }
    bool contains(double latDeg, double lonDeg) const;

    // Nearest sample, zero outside. Nearest rather than bilinear because a
    // stand edge is a discontinuity: interpolating across it invents a ramp of
    // half-height trees that is not there.
    std::uint16_t heightDmAt(double latDeg, double lonDeg) const;
    // Unknown cover answers 100, matching TerrainModel's reading of an empty
    // cover grid.
    std::uint8_t coverPctAt(double latDeg, double lonDeg) const;

    // Resample onto a georeferenced terrain grid. Returns false, leaving the
    // outputs untouched, when the terrain carries no geographic reference or
    // the two footprints do not overlap at all - a caller must not be allowed
    // to install canopy for somewhere else.
    bool sampleOnto(const TerrainModel& terrain,
                    std::vector<std::uint16_t>& outHeightDm,
                    std::vector<std::uint8_t>& outCoverPct) const;
};
