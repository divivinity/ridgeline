#include "data/TerrainLoader.h"
#include "data/CoverageGeoTiff.h"

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL: " #c " line " << __LINE__ << '\n'; ++g_fail; } } while (0)
#define CHECK_NEAR(a,b,t) CHECK(std::abs((double)(a)-(double)(b)) <= (double)(t))

bool writeGeoTiff(const std::string& path, const double gt[6], int width, int height) {
    GDALAllRegister();
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) return false;
    GDALDataset* ds = driver->Create(path.c_str(), width, height, 1, GDT_Float32, nullptr);
    if (!ds) return false;

    ds->SetGeoTransform(const_cast<double*>(gt));
    OGRSpatialReference srs;
    srs.SetWellKnownGeogCS("WGS84");
    char* wkt = nullptr;
    srs.exportToWkt(&wkt);
    ds->SetProjection(wkt);
    CPLFree(wkt);

    std::vector<float> values(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            values[static_cast<size_t>(y) * width + x] = 100.0f + x + y * 2.0f;

    const CPLErr err = ds->GetRasterBand(1)->RasterIO(
        GF_Write, 0, 0, width, height, values.data(), width, height,
        GDT_Float32, 0, 0, nullptr);
    GDALClose(ds);
    return err == CE_None;
}

void testNorthUpRectangular() {
    const std::string path = "test_north_up_rectangular.tif";
    const double gt[6] = {-80.0, 0.01, 0.0, 35.0, 0.0, -0.01};
    CHECK(writeGeoTiff(path, gt, 12, 6));
    try {
        TerrainModel t = TerrainLoader::fromGeoTIFF(path, 64);
        CHECK(t.hasGeoReference());
        CHECK(t.width == 12);
        CHECK(t.height == 6);
        const auto sw = t.worldToLatLon(0.0f, 0.0f);
        const auto ne = t.worldToLatLon(t.worldSize().x, t.worldSize().z);
        CHECK(sw.latDeg < ne.latDeg);
        CHECK(sw.lonDeg < ne.lonDeg);
        CHECK_NEAR(sw.lonDeg, -79.995, 0.011);
        CHECK_NEAR(ne.lonDeg, -79.885, 0.011);
    } catch (const std::exception& e) {
        std::cerr << "FAIL: north-up GeoTIFF threw: " << e.what() << '\n';
        ++g_fail;
    }
    std::remove(path.c_str());
}

void testRotatedRejected() {
    const std::string path = "test_rotated.tif";
    const double gt[6] = {-80.0, 0.01, 0.002, 35.0, 0.001, -0.01};
    CHECK(writeGeoTiff(path, gt, 8, 8));
    bool threw = false;
    try {
        (void)TerrainLoader::fromGeoTIFF(path, 64);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    std::remove(path.c_str());
}
}

// A 3 x 2 result grid of 200 m cells on terrain whose samples are 100 m and
// 0.001 degrees apart, so every expected coordinate is a round number.
void geoTiffFixture(FieldGrid& grid, TerrainModel& terrain) {
    terrain.width = 7;
    terrain.height = 5;
    terrain.setCellSpacing(100.0f, 100.0f);
    terrain.heights.assign(35, 0.0f);
    terrain.setGeoReference(35.0, -80.0, 0.001, 0.001);
    grid.width = 3;
    grid.height = 2;
    grid.cellSize = 200.0f;
    grid.originXZ = {100.0f, 100.0f};
    grid.maxXZ = {600.0f, 400.0f};
    const float none = -std::numeric_limits<float>::infinity();
    grid.dbm = {-70.0f, -75.0f, -80.0f,   // south row
                -85.0f, none, -90.0f};    // north row
}

// An independent reader: GDAL must place, label and mask the coverage export
// exactly as Ridgeline means it.
void testCoverageExportReadsInGdal() {
    FieldGrid grid;
    TerrainModel terrain;
    geoTiffFixture(grid, terrain);
    const std::vector<std::uint8_t> bytes = CoverageGeoTiff::encode(grid, terrain);
    CHECK(!bytes.empty());
    const std::string path = "test_coverage_export.tif";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        CHECK(out.good());
    }

    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    CHECK(ds != nullptr);
    if (!ds) return;
    CHECK(ds->GetRasterXSize() == 3 && ds->GetRasterYSize() == 2);
    double gt[6] = {};
    CHECK(ds->GetGeoTransform(gt) == CE_None);
    CHECK_NEAR(gt[0], -80.0, 1e-9);
    CHECK_NEAR(gt[1], 0.002, 1e-12);
    CHECK_NEAR(gt[2], 0.0, 0.0);
    CHECK_NEAR(gt[3], 35.004, 1e-9);
    CHECK_NEAR(gt[4], 0.0, 0.0);
    CHECK_NEAR(gt[5], -0.002, 1e-12);
    const OGRSpatialReference* srs = ds->GetSpatialRef();
    CHECK(srs != nullptr);
    if (srs) {
        const char* code = srs->GetAuthorityCode(nullptr);
        CHECK(srs->IsGeographic());
        CHECK(code && std::string(code) == "4326");
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    CHECK(band->GetRasterDataType() == GDT_Float32);
    int hasNoData = 0;
    CHECK(std::isnan(band->GetNoDataValue(&hasNoData)) && hasNoData);
    CHECK(std::string(band->GetDescription()) == "Desired signal");
    CHECK(std::string(band->GetUnitType()) == "dBm");
    float values[6] = {};
    CHECK(band->RasterIO(GF_Read, 0, 0, 3, 2, values, 3, 2, GDT_Float32, 0, 0, nullptr)
          == CE_None);
    CHECK_NEAR(values[0], -85.0, 1e-6);
    CHECK(std::isnan(values[1]));
    CHECK_NEAR(values[5], -80.0, 1e-6);
    GDALClose(ds);
    std::remove(path.c_str());
}

int main() {
    testNorthUpRectangular();
    testRotatedRejected();
    testCoverageExportReadsInGdal();
    if (g_fail == 0) std::cout << "GeoTIFF regression tests passed.\n";
    return g_fail ? 1 : 0;
}
