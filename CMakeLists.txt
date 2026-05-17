cmake_minimum_required(VERSION 3.20)
project(Ridgeline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)

# GLM — header only, fetch if not system-installed
include(FetchContent)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# nlohmann/json — header only
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(ridgeline ${SOURCES})

target_include_directories(ridgeline PRIVATE src)
target_link_libraries(ridgeline
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    OpenGL::GL
    glm::glm
    nlohmann_json::nlohmann_json
)

# Copy shaders to build dir alongside binary
add_custom_command(TARGET ridgeline POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:ridgeline>/assets
)

# ── Tests (no Qt / no OpenGL) ──────────────────────────────────────────────
enable_testing()

add_executable(ridgeline_tests
    tests/test_main.cpp
    src/sim/MaterialLibrary.cpp
    src/sim/TerrainModel.cpp
    src/sim/FullSolver.cpp
)
target_include_directories(ridgeline_tests PRIVATE src)
target_link_libraries(ridgeline_tests
    glm::glm
    nlohmann_json::nlohmann_json
)
add_test(NAME physics_tests
    COMMAND ridgeline_tests "${CMAKE_SOURCE_DIR}/assets/materials.json"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
