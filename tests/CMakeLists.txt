add_executable(platter-tests
    support/test_support.cpp
    unit/test_range.cpp
    unit/test_image_device.cpp
    unit/test_partitions.cpp
    unit/test_sysfs.cpp
    unit/test_smart.cpp
    unit/test_operation.cpp
    unit/test_surface.cpp
    unit/test_scan_report.cpp
    unit/test_benchmark.cpp
    unit/test_bench_report.cpp
    unit/test_history.cpp
    unit/test_chacha20.cpp
    unit/test_safety.cpp
    unit/test_erase.cpp
    unit/test_benchmark_write.cpp
    unit/test_surface_write.cpp
    unit/test_partition_edit.cpp
    unit/test_mkfs.cpp
    unit/test_imaging.cpp
    unit/test_inspect.cpp
    unit/test_qualify.cpp
    unit/test_erase_report.cpp
)
if(UNIX)
    target_sources(platter-tests PRIVATE unit/test_helper.cpp)
    target_link_libraries(platter-tests PRIVATE platter-helper-lib)
    target_include_directories(platter-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/helper)
    target_compile_definitions(platter-tests PRIVATE PLATTER_HELPER_PATH="$<TARGET_FILE:platter-helper>")
    add_dependencies(platter-tests platter-helper)
endif()
target_include_directories(platter-tests PRIVATE support)
target_link_libraries(platter-tests PRIVATE platter-core platter-partitions platter-smart platter-surface platter-benchmark platter-erase platter-mkfs platter-imaging platter-inspect platter-qualify platter-reports platter-storage SQLite::SQLite3 Catch2::Catch2WithMain)
target_compile_definitions(platter-tests PRIVATE
    PLATTER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
platter_set_warnings(platter-tests)

include(Catch)
catch_discover_tests(platter-tests)

# Russian translation of the core-library phrases (needs Qt + the compiled .qm)
if(PLATTER_BUILD_GUI AND Qt6LinguistTools_FOUND)
    add_executable(platter-i18n-tests
        unit/test_i18n.cpp
        ${PROJECT_SOURCE_DIR}/src/gui/i18n/i18n.cpp
        ${PROJECT_SOURCE_DIR}/src/gui/i18n/core_patterns.cpp
        ${PROJECT_SOURCE_DIR}/src/gui/i18n/core_phrases.cpp)
    target_include_directories(platter-i18n-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/gui/i18n)
    target_link_libraries(platter-i18n-tests PRIVATE Qt6::Core platter-core platter-smart Catch2::Catch2WithMain)
    target_compile_definitions(platter-i18n-tests PRIVATE PLATTER_TEST_QM="${CMAKE_BINARY_DIR}/src/i18n/platter_ru.qm")
    add_dependencies(platter-i18n-tests platter-translations)
    platter_set_warnings(platter-i18n-tests)
    catch_discover_tests(platter-i18n-tests)
endif()

# The .ts file itself: complete, placeholders intact, plural forms right.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME translations
        COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tools/check_translations.py ${PROJECT_SOURCE_DIR}/translations/platter_ru.ts)
endif()

# Exercise real operation widgets offscreen; no physical devices are opened.
if(PLATTER_BUILD_GUI)
    get_target_property(gui_sources platter-gui SOURCES)
    list(FILTER gui_sources EXCLUDE REGEX "gui/main\\.cpp$|i18n\\.qrc$")
    set(test_gui_sources "")
    foreach(source IN LISTS gui_sources)
        if(NOT IS_ABSOLUTE "${source}")
            list(APPEND test_gui_sources "${PROJECT_SOURCE_DIR}/src/${source}")
        endif()
    endforeach()
    add_executable(platter-gui-tests unit/test_gui.cpp ${test_gui_sources})
    set_target_properties(platter-gui-tests PROPERTIES AUTOMOC ON)
    get_target_property(gui_links platter-gui LINK_LIBRARIES)
    target_link_libraries(platter-gui-tests PRIVATE ${gui_links} Catch2::Catch2)
    if(UNIX)
        target_compile_definitions(platter-gui-tests PRIVATE PLATTER_HAVE_HELPER=1)
    endif()
    add_test(NAME gui-regressions COMMAND platter-gui-tests)
    set_tests_properties(gui-regressions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
