cmake_minimum_required(VERSION 3.24)
project(UniversalPlayableCreatures VERSION 0.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CommonLibSSE CONFIG REQUIRED)

# Use a normal shared library because src/plugin.cpp supplies SKSEPluginInfo /
# SKSEPluginLoad itself. add_commonlibsse_plugin() also generates SKSE plugin
# exports, which causes duplicate SKSEPlugin_Query/SKSEPlugin_Version symbols.
add_library(${PROJECT_NAME} SHARED
    src/plugin.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "UniversalPlayableCreatures"
)
