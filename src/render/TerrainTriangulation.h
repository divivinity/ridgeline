#pragma once
#include <vector>

// The single definition of how a terrain cell is split into two triangles.
//
// Every surface draped over the terrain - the terrain mesh, the imagery drape,
// the coverage heatmap - and every line drawn on it has to agree on this split.
// A cell's four samples are rarely coplanar, so the two possible diagonals give
// two different surfaces: an outline computed on one diagonal while the mesh is
// drawn on the other dips under or floats over the visible ground by up to half
// the cell's twist, and it does so silently.
//
// Samples are tl = (x, z), tr = (x + 1, z), bl = (x, z + 1), br = (x + 1, z + 1),
// where z grows northward. The shared diagonal runs tr-bl, so a point at cell
// fractions (fx, fz) lies in the (tl, bl, tr) triangle when fx + fz <= 1.
namespace TerrainTriangulation {

inline void appendCellIndices(std::vector<unsigned int>& indices,
                              unsigned int tl, unsigned int tr,
                              unsigned int bl, unsigned int br) {
    indices.insert(indices.end(), {tl, bl, tr, tr, bl, br});
}

// Height at cell fractions (fx, fz) on the drawn surface. h10 is the tr sample
// and h01 the bl sample. Linear within each triangle, which is what lets a line
// broken at every grid and diagonal crossing lie exactly on the mesh.
inline float heightInCell(float h00, float h10, float h01, float h11,
                          float fx, float fz) {
    if (fx + fz <= 1.0f)
        return h00 + fx * (h10 - h00) + fz * (h01 - h00);
    return h11 + (1.0f - fx) * (h01 - h11) + (1.0f - fz) * (h10 - h11);
}

// Rise per unit of cell fraction, along x and z, of the triangle holding (fx, fz)
// on the same split heightInCell uses. Divide by the cell size for metres.
inline void slopeInCell(float h00, float h10, float h01, float h11,
                        float fx, float fz, float& perFx, float& perFz) {
    if (fx + fz <= 1.0f) {
        perFx = h10 - h00;
        perFz = h01 - h00;
    } else {
        perFx = h11 - h01;
        perFz = h11 - h10;
    }
}

} // namespace TerrainTriangulation
