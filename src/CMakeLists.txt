# ---------------------------------------------------------------- platter-core
add_library(platter-core STATIC
    core/error.cpp
    core/range.cpp
    core/device.cpp
    core/bytes.cpp
    core/operation.cpp
    platform/image/image_info.cpp
    platform/privileges.cpp
)

if(WIN32)
    target_sources(platter-core PRIVATE
        core/process_win32.cpp
        platform/windows/win32_file_device.cpp
        platform/windows/win32_enumerate.cpp
    )
    target_link_libraries(platter-core PRIVATE setupapi)
    target_compile_definitions(platter-core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
else()
    target_sources(platter-core PRIVATE
        platform/posix/posix_file_device.cpp
        core/process_posix.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(platter-core PRIVATE
            platform/linux/sysfs.cpp
            platform/linux/linux_enumerate.cpp
        )
    else()
        target_sources(platter-core PRIVATE platform/unsupported_enumerate.cpp)
    endif()
endif()

target_include_directories(platter-core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/generated)
target_compile_features(platter-core PUBLIC cxx_std_20)
platter_set_warnings(platter-core)

# ---------------------------------------------------------- platter-partitions
add_library(platter-partitions STATIC partitions/partition_table.cpp)
target_link_libraries(platter-partitions PUBLIC platter-core)
platter_set_warnings(platter-partitions)

# --------------------------------------------------------------- platter-smart
add_library(platter-smart STATIC smart/smart.cpp)
target_link_libraries(platter-smart PUBLIC platter-core PRIVATE nlohmann_json::nlohmann_json)
platter_set_warnings(platter-smart)

# ------------------------------------------------------------- platter-surface
add_library(platter-surface STATIC surface/surface_scan.cpp)
target_link_libraries(platter-surface PUBLIC platter-core)
find_package(Threads REQUIRED)
target_link_libraries(platter-core PUBLIC Threads::Threads)
platter_set_warnings(platter-surface)

# ------------------------------------------------------------- platter-reports
add_library(platter-reports STATIC reports/json.cpp)
target_link_libraries(platter-reports
    PUBLIC platter-core platter-partitions platter-smart nlohmann_json::nlohmann_json)
platter_set_warnings(platter-reports)

# ----------------------------------------------------------------- platter-cli
if(PLATTER_BUILD_CLI)
    add_executable(platter-cli cli/main.cpp)
    target_link_libraries(platter-cli PRIVATE platter-core platter-partitions platter-smart platter-reports)
    platter_set_warnings(platter-cli)
    install(TARGETS platter-cli RUNTIME DESTINATION bin)
endif()

# ----------------------------------------------------------------- platter-gui
if(PLATTER_BUILD_GUI)
    set(CMAKE_AUTOMOC ON)
    add_executable(platter-gui WIN32
        gui/main.cpp
        gui/styles/classic_theme.cpp
        gui/widgets/log_model.cpp
        gui/widgets/table_tools.cpp
        gui/widgets/partition_bar.cpp
        gui/tabs/tabs.cpp
        gui/mainwindow/main_window.cpp
        # headers listed so AUTOMOC sees the Q_OBJECT classes
        gui/widgets/log_model.hpp
        gui/widgets/partition_bar.hpp
        gui/tabs/tabs.hpp
        gui/mainwindow/main_window.hpp
    )
    target_link_libraries(platter-gui PRIVATE
        Qt6::Widgets platter-core platter-partitions platter-smart platter-reports)
    platter_set_warnings(platter-gui)
    install(TARGETS platter-gui RUNTIME DESTINATION bin)
endif()
