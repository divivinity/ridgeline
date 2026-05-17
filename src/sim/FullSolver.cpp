#include "FullSolver.h"
#include <limits>

FieldGrid FullSolver::solve(const TerrainModel&,
                             const std::vector<Antenna>&,
                             const MaterialLibrary&) const {
    return FieldGrid{};
}
