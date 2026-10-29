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
    unit/test_erase_report.cpp
)
target_include_directories(platter-tests PRIVATE support)
target_link_libraries(platter-tests PRIVATE platter-core platter-partitions platter-smart platter-surface platter-benchmark platter-erase platter-mkfs platter-imaging platter-reports platter-storage SQLite::SQLite3 Catch2::Catch2WithMain)
target_compile_definitions(platter-tests PRIVATE
    PLATTER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
platter_set_warnings(platter-tests)

include(Catch)
catch_discover_tests(platter-tests)
