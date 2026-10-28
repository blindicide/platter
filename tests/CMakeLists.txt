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
)
target_include_directories(platter-tests PRIVATE support)
target_link_libraries(platter-tests PRIVATE platter-core platter-partitions platter-smart platter-surface platter-reports Catch2::Catch2WithMain)
target_compile_definitions(platter-tests PRIVATE
    PLATTER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
platter_set_warnings(platter-tests)

include(Catch)
catch_discover_tests(platter-tests)
