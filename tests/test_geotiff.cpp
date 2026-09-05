#include "data/TerrainLoader.h"

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <cmath>
#include <cstdio>
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

int main() {
    testNorthUpRectangular();
    testRotatedRejected();
    if (g_fail == 0) std::cout << "GeoTIFF regression tests passed.\n";
    return g_fail ? 1 : 0;
}
