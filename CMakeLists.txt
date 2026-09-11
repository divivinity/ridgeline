cmake_minimum_required(VERSION 3.20)
project(Ridgeline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Network Concurrent)
find_package(OpenGL REQUIRED)
find_package(GDAL QUIET)

include(FetchContent)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(ntia_itm
    GIT_REPOSITORY https://github.com/NTIA/itm.git
    GIT_TAG        v1.4
)
FetchContent_GetProperties(ntia_itm)
if(NOT ntia_itm_POPULATED)
    FetchContent_Populate(ntia_itm)
endif()

file(GLOB NTIA_ITM_SOURCES CONFIGURE_DEPENDS "${ntia_itm_SOURCE_DIR}/src/*.cpp")
foreach(_itm_src IN LISTS NTIA_ITM_SOURCES)
    file(READ "${_itm_src}" _itm_text)
    string(REPLACE "..\\include\\" "../include/" _itm_text "${_itm_text}")
    file(WRITE "${_itm_src}" "${_itm_text}")
endforeach()
file(READ "${ntia_itm_SOURCE_DIR}/include/itm.h" _itm_header)
string(REPLACE
    "#define DLLEXPORT extern \"C\" __declspec(dllexport)"
    "#ifdef _WIN32\n#define DLLEXPORT extern \"C\" __declspec(dllexport)\n#else\n#define DLLEXPORT extern \"C\"\n#endif"
    _itm_header "${_itm_header}")
file(WRITE "${ntia_itm_SOURCE_DIR}/include/itm.h" "${_itm_header}")

add_library(ntia_itm STATIC ${NTIA_ITM_SOURCES})
target_include_directories(ntia_itm PUBLIC "${ntia_itm_SOURCE_DIR}/include")
target_compile_features(ntia_itm PUBLIC cxx_std_17)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(ridgeline ${SOURCES})
if(WIN32)
    set_target_properties(ridgeline PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(ridgeline PRIVATE src)
target_compile_definitions(ridgeline PRIVATE HAVE_NTIA_ITM)
target_link_libraries(ridgeline
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network Qt6::Concurrent
    OpenGL::GL glm::glm nlohmann_json::nlohmann_json ntia_itm
)
if(GDAL_FOUND)
    target_compile_definitions(ridgeline PRIVATE HAVE_GDAL)
    target_include_directories(ridgeline PRIVATE ${GDAL_INCLUDE_DIRS})
    target_link_libraries(ridgeline GDAL::GDAL)
    message(STATUS "GDAL found — GeoTIFF loading enabled")
else()
    message(STATUS "GDAL not found — GeoTIFF loading disabled (HGT/DTED still work)")
endif()

add_custom_command(TARGET ridgeline POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:ridgeline>/assets
)

if(WIN32)
    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT)
        add_custom_command(TARGET ridgeline POST_BUILD
            COMMAND ${WINDEPLOYQT} $<TARGET_FILE:ridgeline>
            COMMENT "Deploying Qt runtime DLLs...")
    endif()
endif()

enable_testing()

set(RIDGELINE_PHYSICS_TEST_SOURCES
    src/sim/SpectrumCalc.cpp
    src/sim/MaterialLibrary.cpp
    src/sim/TerrainModel.cpp
    src/sim/RFModels.cpp
    src/sim/PointAnalysis.cpp
    src/sim/ScenarioComparison.cpp
    src/sim/ItmPropagation.cpp
    src/sim/FastSolver.cpp
    src/sim/FullSolver.cpp
    src/sim/SimEngine.cpp
    src/sim/SolveSignature.cpp
    src/sim/VegetationAreas.cpp
    src/data/AntennaLibrary.cpp
    src/data/ScenarioIO.cpp
    src/data/TerrainLoader.cpp
)

add_executable(ridgeline_tests tests/test_main.cpp ${RIDGELINE_PHYSICS_TEST_SOURCES})
target_include_directories(ridgeline_tests PRIVATE src)
target_compile_definitions(ridgeline_tests PRIVATE HAVE_NTIA_ITM)
target_link_libraries(ridgeline_tests glm::glm nlohmann_json::nlohmann_json ntia_itm Qt6::Core)
add_test(NAME physics_tests
    COMMAND ridgeline_tests
        "${CMAKE_SOURCE_DIR}/assets/materials.json"
        "${CMAKE_SOURCE_DIR}/assets/antennas.json"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_dted_decoding_tests
    tests/test_dted_decoding.cpp src/sim/TerrainModel.cpp src/data/TerrainLoader.cpp)
target_include_directories(ridgeline_dted_decoding_tests PRIVATE src)
target_link_libraries(ridgeline_dted_decoding_tests glm::glm nlohmann_json::nlohmann_json)
# The published GDAL reference fixtures are committed under tests/data/dted, so
# the checksum and datum-rejection checks run on every build rather than only
# when a developer remembers to pass them. Both paths are always supplied: the
# decoder throws on a missing file, so a dropped fixture fails the suite instead
# of silently reducing it to the synthetic cases.
add_test(NAME dted_decoding_tests COMMAND ridgeline_dted_decoding_tests
        "${CMAKE_SOURCE_DIR}/tests/data/dted/n43.dt0"
        "${CMAKE_SOURCE_DIR}/tests/data/dted/n43_wgs72.dt0"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_layer_tests tests/test_layers.cpp
    src/map/LayerStack.cpp src/map/Hillshade.cpp src/map/TileProviders.cpp
    src/map/MapMeasure.cpp
    src/sim/TerrainModel.cpp src/data/TerrainLoader.cpp src/data/ScenarioIO.cpp)
target_include_directories(ridgeline_layer_tests PRIVATE src)
target_link_libraries(ridgeline_layer_tests
    Qt6::Core Qt6::Gui glm::glm nlohmann_json::nlohmann_json)
add_test(NAME layer_tests COMMAND ridgeline_layer_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_hgt_georef_tests
    tests/test_hgt_georef.cpp src/sim/TerrainModel.cpp src/data/TerrainLoader.cpp)
target_include_directories(ridgeline_hgt_georef_tests PRIVATE src)
target_link_libraries(ridgeline_hgt_georef_tests glm::glm nlohmann_json::nlohmann_json)
add_test(NAME hgt_georef_tests COMMAND ridgeline_hgt_georef_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_hgt_decoding_tests
    tests/test_hgt_decoding.cpp src/sim/TerrainModel.cpp src/data/TerrainLoader.cpp)
target_include_directories(ridgeline_hgt_decoding_tests PRIVATE src)
target_link_libraries(ridgeline_hgt_decoding_tests glm::glm nlohmann_json::nlohmann_json)
add_test(NAME hgt_decoding_tests COMMAND ridgeline_hgt_decoding_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_geospatial_antenna_tests
    tests/test_geospatial_antennas.cpp ${RIDGELINE_PHYSICS_TEST_SOURCES})
target_include_directories(ridgeline_geospatial_antenna_tests PRIVATE src)
target_compile_definitions(ridgeline_geospatial_antenna_tests PRIVATE HAVE_NTIA_ITM)
target_link_libraries(ridgeline_geospatial_antenna_tests
    glm::glm nlohmann_json::nlohmann_json ntia_itm Qt6::Core)
add_test(NAME geospatial_antenna_tests COMMAND ridgeline_geospatial_antenna_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_rf_model_tests
    tests/test_rf_models.cpp
    ${RIDGELINE_PHYSICS_TEST_SOURCES}
    src/data/AntennaPatternIO.cpp)
target_include_directories(ridgeline_rf_model_tests PRIVATE src)
target_compile_definitions(ridgeline_rf_model_tests PRIVATE HAVE_NTIA_ITM)
target_link_libraries(ridgeline_rf_model_tests
    glm::glm nlohmann_json::nlohmann_json ntia_itm Qt6::Core)

add_executable(ridgeline_scenario_save_tests tests/test_scenario_save.cpp
    src/data/ScenarioIO.cpp src/data/RecoveryStore.cpp src/data/WorkspaceSettings.cpp)
target_include_directories(ridgeline_scenario_save_tests PRIVATE src)
target_link_libraries(ridgeline_scenario_save_tests Qt6::Core glm::glm nlohmann_json::nlohmann_json)
add_test(NAME scenario_save_tests COMMAND ridgeline_scenario_save_tests)
add_test(NAME rf_model_tests COMMAND ridgeline_rf_model_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_scenario_comparison_tests
    tests/test_scenario_comparison.cpp
    src/sim/ScenarioComparison.cpp)
target_include_directories(ridgeline_scenario_comparison_tests PRIVATE src)
target_link_libraries(ridgeline_scenario_comparison_tests glm::glm)
add_test(NAME scenario_comparison_tests
    COMMAND ridgeline_scenario_comparison_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_measurement_calibration_tests
    tests/test_measurement_calibration.cpp
    src/sim/MeasurementCalibration.cpp
    src/sim/TerrainModel.cpp
    src/data/MeasurementIO.cpp)
target_include_directories(ridgeline_measurement_calibration_tests PRIVATE src)
target_link_libraries(ridgeline_measurement_calibration_tests glm::glm)
add_test(NAME measurement_calibration_tests
    COMMAND ridgeline_measurement_calibration_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_tile_cache_tests
    tests/test_tile_cache.cpp src/data/TileBatchDownloader.cpp)
target_include_directories(ridgeline_tile_cache_tests PRIVATE src)
target_link_libraries(ridgeline_tile_cache_tests Qt6::Network)
add_test(NAME tile_cache_tests COMMAND ridgeline_tile_cache_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_global_terrain_tests
    tests/test_global_terrain.cpp src/data/GlobalTerrainLoader.cpp
    src/data/TileBatchDownloader.cpp src/sim/TerrainModel.cpp)
target_include_directories(ridgeline_global_terrain_tests PRIVATE src)
target_link_libraries(ridgeline_global_terrain_tests Qt6::Gui Qt6::Network glm::glm)
add_test(NAME global_terrain_tests COMMAND ridgeline_global_terrain_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_location_picking_tests
    tests/test_location_and_picking.cpp
    src/geo/Mgrs.cpp
    src/render/TerrainRaycast.cpp
    src/sim/TerrainModel.cpp)
target_include_directories(ridgeline_location_picking_tests PRIVATE src)
target_link_libraries(ridgeline_location_picking_tests glm::glm)
add_test(NAME location_picking_tests COMMAND ridgeline_location_picking_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_ui_interaction_tests
    tests/test_ui_interactions.cpp
    src/ui/OverlayToolbar.cpp
    src/ui/LayersPanel.cpp
    src/ui/MapFurniture.cpp
    src/map/LayerStack.cpp
    src/map/MapMeasure.cpp
    src/ui/ScenarioTree.cpp
    src/ui/PropertiesPanel.cpp
    src/ui/LocationDialog.cpp
    src/ui/SourcePositionDialog.cpp
    src/ui/PointAnalysisPanel.cpp
    src/ui/ScenarioComparisonPanel.cpp
    src/ui/CalibrationPanel.cpp
    src/geo/Mgrs.cpp
    src/sim/TerrainModel.cpp
    src/data/AntennaLibrary.cpp
    src/data/AntennaPatternIO.cpp
    src/data/Exporter.cpp)
target_include_directories(ridgeline_ui_interaction_tests PRIVATE src)
target_link_libraries(ridgeline_ui_interaction_tests
    Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network glm::glm nlohmann_json::nlohmann_json)
add_test(NAME ui_interaction_tests COMMAND ridgeline_ui_interaction_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
if(TARGET Qt6::QOffscreenIntegrationPlugin)
    # windeployqt deploys only the native platform by default. Let this
    # development test find the matching SDK plugin when offscreen is selected;
    # do not change the application's deployment or force a headless backend.
    set_tests_properties(ui_interaction_tests PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
endif()

if(GDAL_FOUND)
    add_executable(ridgeline_geotiff_tests
        tests/test_geotiff.cpp src/sim/TerrainModel.cpp src/data/TerrainLoader.cpp)
    target_include_directories(ridgeline_geotiff_tests PRIVATE src ${GDAL_INCLUDE_DIRS})
    target_compile_definitions(ridgeline_geotiff_tests PRIVATE HAVE_GDAL)
    target_link_libraries(ridgeline_geotiff_tests
        glm::glm nlohmann_json::nlohmann_json GDAL::GDAL)
    add_test(NAME geotiff_tests COMMAND ridgeline_geotiff_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# ── MainWindow GUI tests (need a real display) ─────────────────────────────
# ui_interaction_tests runs on the offscreen platform and links only the panels
# and dialogs. MainWindow owns a QOpenGLWidget, which offscreen cannot give a
# context, so covering it needs a display — CI runs these under xvfb-run.
#
# Off by default, so an ordinary `cmake && ctest` is completely unaffected.
# When enabled the app sources are compiled a second time rather than the app
# target being restructured into a library, which keeps this additive: no
# existing target changes shape.
option(RIDGELINE_GUI_TESTS
    "Build the MainWindow GUI tests (requires a display, e.g. xvfb-run)" OFF)

if(RIDGELINE_GUI_TESTS)
    set(RIDGELINE_APP_SOURCES ${SOURCES})
    list(REMOVE_ITEM RIDGELINE_APP_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)

    add_executable(ridgeline_main_window_tests
        tests/test_main_window_guard.cpp ${RIDGELINE_APP_SOURCES})
    target_include_directories(ridgeline_main_window_tests PRIVATE src)
    target_compile_definitions(ridgeline_main_window_tests PRIVATE HAVE_NTIA_ITM)
    target_link_libraries(ridgeline_main_window_tests
        Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network Qt6::Concurrent
        OpenGL::GL glm::glm nlohmann_json::nlohmann_json ntia_itm)
    if(GDAL_FOUND)
        target_compile_definitions(ridgeline_main_window_tests PRIVATE HAVE_GDAL)
        target_include_directories(ridgeline_main_window_tests PRIVATE ${GDAL_INCLUDE_DIRS})
        target_link_libraries(ridgeline_main_window_tests GDAL::GDAL)
    endif()

    # Runs beside the app's copied assets, which MainWindow loads at startup.
    add_dependencies(ridgeline_main_window_tests ridgeline)
    add_test(NAME main_window_tests COMMAND ridgeline_main_window_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
