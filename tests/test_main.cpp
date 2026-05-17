#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include "sim/MaterialLibrary.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)
#define CHECK_NEAR(a, b, tol) CHECK(std::abs((float)(a)-(float)(b)) < (float)(tol))

static void test_default_material() {
    const Material& m = MaterialLibrary::defaultMaterial();
    CHECK(m.name == "dry_soil");
    CHECK_NEAR(m.epsilon_r, 3.0f, 0.01f);
    CHECK_NEAR(m.sigma_sm,  0.001f, 1e-5f);
    CHECK(!m.bandLoss.empty());
}

static void test_loss_interpolation() {
    const Material& m = MaterialLibrary::defaultMaterial();
    CHECK_NEAR(m.lossDbPer100m(100.0f), 0.3f, 0.01f);
    CHECK_NEAR(m.lossDbPer100m(300.0f), 0.5f, 0.01f);
    // At 200 MHz, linear interpolation between 100 MHz (0.3) and 300 MHz (0.5):
    // t = (200-100)/(300-100) = 0.5  →  0.3 + 0.5*(0.5-0.3) = 0.4
    CHECK_NEAR(m.lossDbPer100m(200.0f), 0.4f, 0.01f);
    CHECK_NEAR(m.lossDbPer100m(10.0f),    0.3f, 0.01f);  // clamp low
    CHECK_NEAR(m.lossDbPer100m(10000.0f), 3.0f, 0.01f);  // clamp high
}

static void test_load_from_json(const std::string& path) {
    MaterialLibrary lib;
    lib.load(path);

    const Material& concrete = lib.find("concrete");
    CHECK(concrete.name == "concrete");
    CHECK_NEAR(concrete.epsilon_r, 5.5f, 0.01f);
    CHECK_NEAR(concrete.sigma_sm,  0.02f, 1e-4f);
    CHECK_NEAR(concrete.lossDbPer100m(1000.0f), 5.0f, 0.01f);

    const Material& unknown = lib.find("unobtainium");
    CHECK(unknown.name == "dry_soil");

    for (const char* name : {"dry_soil","wet_soil","concrete","foliage","asphalt","water"}) {
        CHECK(lib.find(name).name == std::string(name));
    }
}

int main(int argc, char** argv) {
    std::string materialsPath = (argc > 1) ? argv[1] : "assets/materials.json";

    test_default_material();
    test_loss_interpolation();
    test_load_from_json(materialsPath);

    std::cout << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail ? 1 : 0;
}
