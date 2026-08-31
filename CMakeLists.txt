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

# NTIA's official Irregular Terrain Model (Longley-Rice) reference
# implementation. v1.4 is pinned for reproducible predictions. The upstream
# v1.4 source uses Windows path separators and a Windows DLL export macro even
# though the algorithms are portable; normalize those two build-surface details
# in the fetched copy without changing propagation code.
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

target_include_directories(ridgeline PRIVATE src)
target_compile_definitions(ridgeline PRIVATE HAVE_NTIA_ITM)
target_link_libraries(ridgeline
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Network
    Qt6::Concurrent
    OpenGL::GL
    glm::glm
    nlohmann_json::nlohmann_json
    ntia_itm
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
    find_program(WINDEPLOYQT windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
    )
    if(WINDEPLOYQT)
        add_custom_command(TARGET ridgeline POST_BUILD
            COMMAND ${WINDEPLOYQT}
                $<TARGET_FILE:ridgeline>
            COMMENT "Deploying Qt runtime DLLs..."
        )
    endif()
endif()

enable_testing()

set(RIDGELINE_PHYSICS_TEST_SOURCES
    src/sim/SpectrumCalc.cpp
    src/sim/MaterialLibrary.cpp
    src/sim/TerrainModel.cpp
    src/sim/RFModels.cpp
    src/sim/ItmPropagation.cpp
    src/sim/FastSolver.cpp
    src/sim/FullSolver.cpp
    src/sim/SimEngine.cpp
    src/data/AntennaLibrary.cpp
    src/data/ScenarioIO.cpp
    src/data/TerrainLoader.cpp
)

add_executable(ridgeline_tests
    tests/test_main.cpp
    ${RIDGELINE_PHYSICS_TEST_SOURCES}
)
target_include_directories(ridgeline_tests PRIVATE src)
target_compile_definitions(ridgeline_tests PRIVATE HAVE_NTIA_ITM)
target_link_libraries(ridgeline_tests
    glm::glm
    nlohmann_json::nlohmann_json
    ntia_itm
)
add_test(NAME physics_tests
    COMMAND ridgeline_tests
        "${CMAKE_SOURCE_DIR}/assets/materials.json"
        "${CMAKE_SOURCE_DIR}/assets/antennas.json"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_hgt_georef_tests
    tests/test_hgt_georef.cpp
    src/sim/TerrainModel.cpp
    src/data/TerrainLoader.cpp
)
target_include_directories(ridgeline_hgt_georef_tests PRIVATE src)
target_link_libraries(ridgeline_hgt_georef_tests
    glm::glm
    nlohmann_json::nlohmann_json
)
add_test(NAME hgt_georef_tests
    COMMAND ridgeline_hgt_georef_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ridgeline_geospatial_antenna_tests
    tests/test_geospatial_antennas.cpp
    ${RIDGELINE_PHYSICS_TEST_SOURCES}
)
target_include_directories(ridgeline_geospatial_antenna_tests PRIVATE src)
target_compile_definitions(ridgeline_geospatial_antenna_tests PRIVATE HAVE_NTIA_ITM)
target_link_libraries(ridgeline_geospatial_antenna_tests
    glm::glm
    nlohmann_json::nlohmann_json
    ntia_itm
)
add_test(NAME geospatial_antenna_tests
    COMMAND ridgeline_geospatial_antenna_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if(GDAL_FOUND)
    add_executable(ridgeline_geotiff_tests
        tests/test_geotiff.cpp
        src/sim/TerrainModel.cpp
        src/data/TerrainLoader.cpp
    )
    target_include_directories(ridgeline_geotiff_tests PRIVATE src ${GDAL_INCLUDE_DIRS})
    target_compile_definitions(ridgeline_geotiff_tests PRIVATE HAVE_GDAL)
    target_link_libraries(ridgeline_geotiff_tests
        glm::glm
        nlohmann_json::nlohmann_json
        GDAL::GDAL
    )
    add_test(NAME geotiff_tests
        COMMAND ridgeline_geotiff_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
