#pragma once

#include "CanopyRaster.h"
#include "LandfireCanopy.h"
#include "sim/TerrainModel.h"

#include <QString>
#include <functional>

class QNetworkAccessManager;

// Fetches a canopy layer covering exactly the ground the loaded terrain covers.
//
// Requests go through the same batch downloader as terrain and imagery, so the
// responses land in the portable tile cache: a scenario reopened offline finds
// its canopy already there, and an offline package can carry one.
class CanopyLoader {
public:
    using ProgressCallback = std::function<void(int completed, int total)>;
    using CancelCallback = std::function<bool()>;

    struct Result {
        CanopyRaster canopy;
        // Empty when the canopy is usable. Anything else is a reason to leave
        // the existing vegetation alone and tell the operator why.
        QString error;
        // Worth saying even on success - partial coverage, a large share of
        // assigned heights.
        QString note;

        bool ok() const { return error.isEmpty() && canopy.valid(); }
    };

    // The terrain must be georeferenced: without that there is no way to know
    // what ground to ask about.
    static Result loadForTerrain(QNetworkAccessManager& network,
                                 const TerrainModel& terrain,
                                 const landfire::Options& options = {},
                                 const ProgressCallback& progress = {},
                                 const CancelCallback& cancel = {});

    // Largest grid requested per axis. The provider would serve far more, but
    // beyond the terrain's own sample count the extra pixels are detail no
    // solve can use.
    static constexpr int kMaxSamplesPerAxis = 1024;
};
