cmake_minimum_required(VERSION 3.24)
project(UniversalPlayableCreatures VERSION 0.2.13 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# One DLL owns both playability utilities and the Universal Creature Controls core.
# src/plugin.cpp is the sole SKSE entry point; ucc_core.cpp exposes only an internal
# initializer and therefore cannot create duplicate SKSE exports.
add_library(${PROJECT_NAME} SHARED
    src/plugin.cpp
    src/RaceCatalog.cpp
    src/ucc_core.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
    nlohmann_json::nlohmann_json
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /W4)
endif()

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "UniversalPlayableCreatures"
)
