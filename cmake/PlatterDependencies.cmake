# Prefer system packages; fall back to FetchContent (used on Windows CI).
include(FetchContent)

find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, fetching")
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    # TODO: pin URL_HASH once verified from a trusted checksum.
    FetchContent_MakeAvailable(json)
endif()

if(PLATTER_BUILD_TESTS)
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        message(STATUS "Catch2 v3 not found, fetching")
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.5.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    endif()
endif()

if(PLATTER_BUILD_GUI)
    find_package(Qt6 6.2 COMPONENTS Widgets QUIET)
    if(NOT Qt6_FOUND)
        message(WARNING "Qt 6 Widgets not found: GUI will not be built")
        set(PLATTER_BUILD_GUI OFF CACHE BOOL "" FORCE)
    endif()
endif()

# SQLite: system library when available (Linux), vendored amalgamation otherwise.
option(PLATTER_SYSTEM_SQLITE "Prefer the system SQLite 3 library" ON)
if(PLATTER_SYSTEM_SQLITE)
    find_package(SQLite3 QUIET)
endif()
if(NOT TARGET SQLite::SQLite3)
    message(STATUS "Using vendored SQLite amalgamation (third_party/sqlite3)")
    add_subdirectory(${PROJECT_SOURCE_DIR}/third_party ${PROJECT_BINARY_DIR}/third_party)
endif()

# zstd (compressed images): system library when available, FetchContent otherwise.
option(PLATTER_WITH_ZSTD "Support zstd-compressed disk images" ON)
option(PLATTER_FETCH_ZSTD "Always fetch zstd instead of using the system library" OFF)
if(PLATTER_WITH_ZSTD)
    if(NOT PLATTER_FETCH_ZSTD)
        find_path(PLATTER_ZSTD_INCLUDE_DIR zstd.h)
        find_library(PLATTER_ZSTD_LIBRARY NAMES zstd zstd_static libzstd)
    endif()
    if(PLATTER_ZSTD_INCLUDE_DIR AND PLATTER_ZSTD_LIBRARY AND NOT PLATTER_FETCH_ZSTD)
        message(STATUS "Using system zstd: ${PLATTER_ZSTD_LIBRARY}")
        add_library(platter-zstd INTERFACE)
        target_include_directories(platter-zstd INTERFACE ${PLATTER_ZSTD_INCLUDE_DIR})
        target_link_libraries(platter-zstd INTERFACE ${PLATTER_ZSTD_LIBRARY})
    else()
        message(STATUS "zstd not found (or fetch forced): fetching v1.5.6")
        set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
        set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(zstd
            URL https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz
            SOURCE_SUBDIR build/cmake
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(zstd)
        add_library(platter-zstd INTERFACE)
        target_link_libraries(platter-zstd INTERFACE libzstd_static)
        target_include_directories(platter-zstd INTERFACE ${zstd_SOURCE_DIR}/lib)
    endif()
endif()
