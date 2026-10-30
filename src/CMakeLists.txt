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
    core/broker.cpp
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

# ------------------------------------------------------------- platter-helper
# Privilege separation (POSIX only): a root helper process plus the client
# that lets unprivileged programs open devices through it.
if(UNIX)
    add_library(platter-helper-lib STATIC helper/wire.cpp helper/server.cpp helper/client.cpp)
    target_link_libraries(platter-helper-lib PUBLIC platter-core)
    platter_set_warnings(platter-helper-lib)
    add_executable(platter-helper helper/helper_main.cpp)
    target_link_libraries(platter-helper PRIVATE platter-helper-lib)
    platter_set_warnings(platter-helper)
    install(TARGETS platter-helper RUNTIME DESTINATION libexec/platter)
endif()

# ---------------------------------------------------------- platter-partitions
add_library(platter-partitions STATIC partitions/partition_table.cpp partitions/partition_edit.cpp)
target_link_libraries(platter-partitions PUBLIC platter-core)
platter_set_warnings(platter-partitions)

# ------------------------------------------------------------- platter-inspect
add_library(platter-inspect STATIC inspect/inspect.cpp)
target_link_libraries(platter-inspect PUBLIC platter-core platter-partitions)
platter_set_warnings(platter-inspect)

# ------------------------------------------------------------ platter-qualify
add_library(platter-qualify STATIC qualify/qualify.cpp)
target_link_libraries(platter-qualify PUBLIC platter-core platter-smart)
platter_set_warnings(platter-qualify)

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
    reports/erase_report.cpp reports/imaging_report.cpp reports/qualify_report.cpp)
target_link_libraries(platter-reports
    PUBLIC platter-core platter-partitions platter-smart platter-surface platter-benchmark platter-erase platter-imaging platter-qualify
           nlohmann_json::nlohmann_json)
platter_set_warnings(platter-reports)

# ------------------------------------------------------------- platter-storage
add_library(platter-storage STATIC storage/history.cpp)
target_link_libraries(platter-storage PUBLIC platter-reports PRIVATE SQLite::SQLite3)
platter_set_warnings(platter-storage)

# ----------------------------------------------------------------- platter-cli
if(WIN32)
    enable_language(RC)
    configure_file(${PROJECT_SOURCE_DIR}/packaging/windows/platter.rc.in
        ${PROJECT_BINARY_DIR}/generated/platter.rc @ONLY)
endif()

if(PLATTER_BUILD_CLI)
    add_executable(platter-cli cli/main.cpp cli/cmd_erase.cpp cli/cmd_partitions.cpp cli/cmd_format.cpp cli/cmd_imaging.cpp cli/cmd_inspect.cpp cli/cmd_qualify.cpp cli/cmd_history.cpp)
    target_link_libraries(platter-cli PRIVATE platter-core platter-partitions platter-smart platter-surface
        platter-benchmark platter-erase platter-mkfs platter-imaging platter-inspect platter-qualify platter-reports platter-storage)
    platter_set_warnings(platter-cli)
    if(WIN32)
        target_sources(platter-cli PRIVATE ${PROJECT_BINARY_DIR}/generated/platter.rc)
    endif()
    if(UNIX)
        target_link_libraries(platter-cli PRIVATE platter-helper-lib)
        target_compile_definitions(platter-cli PRIVATE PLATTER_HAVE_HELPER=1)
    endif()
    install(TARGETS platter-cli RUNTIME DESTINATION ${PLATTER_RUNTIME_DESTINATION})
endif()

# ----------------------------------------------------------------- platter-gui
if(PLATTER_BUILD_GUI)
    set(CMAKE_AUTOMOC ON)
    add_executable(platter-gui WIN32
        gui/main.cpp
        gui/assets.qrc
        gui/styles/classic_theme.cpp
        gui/widgets/log_model.cpp
        gui/widgets/table_tools.cpp
        gui/widgets/partition_bar.cpp
        gui/widgets/partition_pick_button.cpp
        gui/tabs/tabs.cpp
        gui/tabs/surface_tab.cpp
        gui/tabs/benchmark_tab.cpp
        gui/tabs/erase_tab.cpp
        gui/tabs/imaging_tab.cpp
        gui/tabs/hex_tab.cpp
        gui/tabs/qualify_tab.cpp
        gui/widgets/surface_map.cpp
        gui/widgets/chart.cpp
        gui/widgets/confirm_dialog.cpp
        gui/widgets/partition_editor.cpp
        gui/widgets/history_dialog.cpp
        gui/widgets/preferences_dialog.cpp
        gui/widgets/help_dialog.cpp
        gui/widgets/checksum_dialog.cpp
        gui/mainwindow/main_window.cpp
        gui/i18n/i18n.cpp
        gui/i18n/core_patterns.cpp
        gui/i18n/core_phrases.cpp
        # headers listed so AUTOMOC sees the Q_OBJECT classes
        gui/widgets/log_model.hpp
        gui/widgets/partition_bar.hpp
        gui/widgets/partition_pick_button.hpp
        gui/widgets/history_dialog.hpp
        gui/widgets/preferences_dialog.hpp
        gui/widgets/help_dialog.hpp
        gui/widgets/checksum_dialog.hpp
        gui/tabs/tabs.hpp
        gui/tabs/surface_tab.hpp
        gui/tabs/benchmark_tab.hpp
        gui/tabs/erase_tab.hpp
        gui/tabs/hex_tab.hpp
        gui/tabs/qualify_tab.hpp
        gui/widgets/surface_map.hpp
        gui/widgets/chart.hpp
        gui/mainwindow/main_window.hpp
        gui/i18n/i18n.hpp
        gui/i18n/core_patterns.hpp
    )
    target_link_libraries(platter-gui PRIVATE
        Qt6::Widgets platter-core platter-partitions platter-smart platter-surface platter-benchmark
        platter-erase platter-mkfs platter-imaging platter-inspect platter-qualify platter-reports platter-storage)
    platter_set_warnings(platter-gui)
    set_target_properties(platter-gui PROPERTIES AUTORCC ON)
    if(WIN32)
        target_sources(platter-gui PRIVATE ${PROJECT_BINARY_DIR}/generated/platter.rc)
    endif()

    # ---- translations: translations/platter_<lang>.ts -> .qm embedded as :/i18n/
    set(PLATTER_TS_FILES ${PROJECT_SOURCE_DIR}/translations/platter_ru.ts)
    if(Qt6LinguistTools_FOUND)
        set(PLATTER_QM_DIR ${CMAKE_CURRENT_BINARY_DIR}/i18n)
        file(MAKE_DIRECTORY ${PLATTER_QM_DIR})
        set(PLATTER_QM_FILES "")
        set(PLATTER_QRC_BODY "")
        foreach(ts ${PLATTER_TS_FILES})
            get_filename_component(base ${ts} NAME_WE)
            set(qm ${PLATTER_QM_DIR}/${base}.qm)
            add_custom_command(OUTPUT ${qm}
                COMMAND Qt6::lrelease ${ts} -qm ${qm}
                DEPENDS ${ts}
                COMMENT "Compiling ${base}.qm" VERBATIM)
            list(APPEND PLATTER_QM_FILES ${qm})
            string(APPEND PLATTER_QRC_BODY "    <file alias=\"${base}.qm\">${qm}</file>\n")
        endforeach()
        file(WRITE ${PLATTER_QM_DIR}/i18n.qrc "<RCC>\n  <qresource prefix=\"/i18n\">\n${PLATTER_QRC_BODY}  </qresource>\n</RCC>\n")
        add_custom_target(platter-translations DEPENDS ${PLATTER_QM_FILES})
        add_dependencies(platter-gui platter-translations)
        set_target_properties(platter-gui PROPERTIES AUTORCC ON)
        set_source_files_properties(${PLATTER_QM_FILES} PROPERTIES GENERATED TRUE)
        target_sources(platter-gui PRIVATE ${PLATTER_QM_DIR}/i18n.qrc)
        set_property(SOURCE ${PLATTER_QM_DIR}/i18n.qrc PROPERTY OBJECT_DEPENDS ${PLATTER_QM_FILES})
        target_compile_definitions(platter-gui PRIVATE PLATTER_HAVE_TRANSLATIONS=1)
        # Developer convenience: cmake --build build --target update-translations
        file(GLOB_RECURSE PLATTER_GUI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gui/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/gui/*.hpp)
        add_custom_target(update-translations
            COMMAND Qt6::lupdate ${PLATTER_GUI_SOURCES} -ts ${PLATTER_TS_FILES} -no-obsolete -locations none
            COMMENT "Updating .ts files from the GUI sources" VERBATIM)
    endif()

    if(UNIX)
        target_link_libraries(platter-gui PRIVATE platter-helper-lib)
        target_compile_definitions(platter-gui PRIVATE PLATTER_HAVE_HELPER=1)
    endif()
    install(TARGETS platter-gui RUNTIME DESTINATION ${PLATTER_RUNTIME_DESTINATION})
endif()

# Portable Windows entry points: the full runtime lives alongside the real EXEs in app/.
if(WIN32)
    if(PLATTER_BUILD_GUI)
        add_executable(platter-launcher WIN32 ${PROJECT_SOURCE_DIR}/packaging/windows/launcher.cpp
            ${PROJECT_BINARY_DIR}/generated/platter.rc)
        set_target_properties(platter-launcher PROPERTIES OUTPUT_NAME platter)
        target_link_libraries(platter-launcher PRIVATE user32)
        platter_set_warnings(platter-launcher)
    endif()
    if(PLATTER_BUILD_CLI)
        add_executable(platter-cli-launcher ${PROJECT_SOURCE_DIR}/packaging/windows/launcher.cpp
            ${PROJECT_BINARY_DIR}/generated/platter.rc)
        target_compile_definitions(platter-cli-launcher PRIVATE PLATTER_LAUNCH_CLI=1)
        set_target_properties(platter-cli-launcher PROPERTIES OUTPUT_NAME platter-cli)
        platter_set_warnings(platter-cli-launcher)
    endif()
    foreach(launcher platter-launcher platter-cli-launcher)
        if(TARGET ${launcher})
            set_target_properties(${launcher} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/launchers"
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            if(MINGW)
                target_link_options(${launcher} PRIVATE -static -static-libgcc -static-libstdc++)
            endif()
            install(TARGETS ${launcher} RUNTIME DESTINATION .)
        endif()
    endforeach()
endif()
