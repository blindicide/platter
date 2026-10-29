# ---------------------------------------------------------------- platter-core
add_library(platter-core STATIC
    core/error.cpp
    core/range.cpp
    core/device.cpp
    core/bytes.cpp
    core/operation.cpp
    core/histogram.cpp
    core/sha256.cpp
    core/chacha20.cpp
    core/time_util.cpp
    core/safety.cpp
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
add_library(platter-partitions STATIC partitions/partition_table.cpp partitions/partition_edit.cpp)
target_link_libraries(platter-partitions PUBLIC platter-core)
platter_set_warnings(platter-partitions)

# ------------------------------------------------------------- platter-imaging
add_library(platter-imaging STATIC imaging/image_file.cpp imaging/imaging.cpp)
target_link_libraries(platter-imaging PUBLIC platter-core)
if(TARGET platter-zstd)
    target_link_libraries(platter-imaging PUBLIC platter-zstd)
    target_compile_definitions(platter-imaging PUBLIC PLATTER_HAVE_ZSTD=1)
endif()
platter_set_warnings(platter-imaging)

# ---------------------------------------------------------------- platter-mkfs
add_library(platter-mkfs STATIC mkfs/mkfs.cpp)
target_link_libraries(platter-mkfs PUBLIC platter-core)
platter_set_warnings(platter-mkfs)

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

# ----------------------------------------------------------- platter-benchmark
add_library(platter-benchmark STATIC benchmark/benchmark.cpp)
target_link_libraries(platter-benchmark PUBLIC platter-core)
platter_set_warnings(platter-benchmark)

# --------------------------------------------------------------- platter-erase
add_library(platter-erase STATIC erase/erase.cpp)
target_link_libraries(platter-erase PUBLIC platter-core)
platter_set_warnings(platter-erase)

# ------------------------------------------------------------- platter-reports
add_library(platter-reports STATIC reports/json.cpp reports/scan_report.cpp reports/bench_report.cpp
    reports/erase_report.cpp reports/imaging_report.cpp)
target_link_libraries(platter-reports
    PUBLIC platter-core platter-partitions platter-smart platter-surface platter-benchmark platter-erase platter-imaging
           nlohmann_json::nlohmann_json)
platter_set_warnings(platter-reports)

# ------------------------------------------------------------- platter-storage
add_library(platter-storage STATIC storage/history.cpp)
target_link_libraries(platter-storage PUBLIC platter-reports PRIVATE SQLite::SQLite3)
platter_set_warnings(platter-storage)

# ----------------------------------------------------------------- platter-cli
if(PLATTER_BUILD_CLI)
    add_executable(platter-cli cli/main.cpp cli/cmd_erase.cpp cli/cmd_partitions.cpp cli/cmd_format.cpp cli/cmd_imaging.cpp)
    target_link_libraries(platter-cli PRIVATE platter-core platter-partitions platter-smart platter-surface
        platter-benchmark platter-erase platter-mkfs platter-imaging platter-reports platter-storage)
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
        gui/tabs/surface_tab.cpp
        gui/tabs/benchmark_tab.cpp
        gui/tabs/erase_tab.cpp
        gui/tabs/imaging_tab.cpp
        gui/widgets/surface_map.cpp
        gui/widgets/chart.cpp
        gui/widgets/confirm_dialog.cpp
        gui/widgets/partition_editor.cpp
        gui/mainwindow/main_window.cpp
        # headers listed so AUTOMOC sees the Q_OBJECT classes
        gui/widgets/log_model.hpp
        gui/widgets/partition_bar.hpp
        gui/tabs/tabs.hpp
        gui/tabs/surface_tab.hpp
        gui/tabs/benchmark_tab.hpp
        gui/tabs/erase_tab.hpp
        gui/widgets/surface_map.hpp
        gui/widgets/chart.hpp
        gui/mainwindow/main_window.hpp
    )
    target_link_libraries(platter-gui PRIVATE
        Qt6::Widgets platter-core platter-partitions platter-smart platter-surface platter-benchmark
        platter-erase platter-mkfs platter-imaging platter-reports platter-storage)
    platter_set_warnings(platter-gui)
    install(TARGETS platter-gui RUNTIME DESTINATION bin)
endif()
