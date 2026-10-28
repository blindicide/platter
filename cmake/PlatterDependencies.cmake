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
