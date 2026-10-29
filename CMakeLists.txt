cmake_minimum_required(VERSION 3.21)

project(platter
    VERSION 0.5.0
    DESCRIPTION "Cross-platform storage diagnostics, maintenance and management utility"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PLATTER_BUILD_GUI   "Build the Qt 6 Widgets GUI"        ON)
option(PLATTER_BUILD_CLI   "Build platter-cli"                ON)
option(PLATTER_BUILD_TESTS "Build the unit test suite"        ON)
option(PLATTER_WERROR      "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PlatterDependencies)
include(PlatterWarnings)

configure_file(cmake/version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/platter/version.hpp" @ONLY)

add_subdirectory(src)

if(PLATTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
