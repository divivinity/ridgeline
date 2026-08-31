#define main ridgeline_original_test_main
#include "test_main.cpp"
#undef main

#define RUN_TEST(call) do { \
    std::cout << "[RUN] " #call << std::endl; \
    call; \
} while (0)

int main(int argc, char** argv) {
    std::string materialsPath = (argc > 1) ? argv[1] : "assets/materials.json";
    std::string antennasPath = (argc > 2) ? argv[2] : "assets/antennas.json";

    RUN_TEST(test_default_material());
    RUN_TEST(test_loss_interpolation());
    RUN_TEST(test_load_from_json(materialsPath));
    RUN_TEST(test_peak_obstacle_flat_terrain());
    RUN_TEST(test_peak_obstacle_blocked_terrain());
    RUN_TEST(test_georeferenced_world_round_trip());
    RUN_TEST(test_fullsolver_produces_finite_grid());
    RUN_TEST(test_rain_reduces_power());
    RUN_TEST(test_ridge_reduces_power_vs_flat());
    RUN_TEST(test_antenna_library_defaults());
    RUN_TEST(test_antenna_library_load_json(antennasPath));
    RUN_TEST(test_antenna_library_load_defaults());
    RUN_TEST(test_add_antenna());
    RUN_TEST(test_remove_antenna());
    RUN_TEST(test_update_antenna());
    RUN_TEST(test_scenario_io_hills_round_trip());
    RUN_TEST(test_scenario_io_hgt_round_trip());
    RUN_TEST(test_scenario_io_planner_round_trip());
    RUN_TEST(test_scenario_io_global_round_trip());
    RUN_TEST(test_spectrum_length());
    RUN_TEST(test_spectrum_freq_range());
    RUN_TEST(test_spectrum_higher_freq_lower_power());
    RUN_TEST(test_spectrum_power_reasonable());
    RUN_TEST(test_spectrum_single_step());
    RUN_TEST(test_spectrum_throws_on_invalid_steps());
    RUN_TEST(test_spectrum_throws_on_zero_dist());
    RUN_TEST(test_spectrum_throws_on_zero_power());

    std::cout << g_pass << " passed, " << g_fail << " failed." << std::endl;
    return g_fail ? 1 : 0;
}
