# ---------------------------------------------------------------- platter-core
add_library(platter-core STATIC
    core/error.cpp
    core/range.cpp
    core/device.cpp
    core/bytes.cpp
    platform/image/image_info.cpp
)

if(WIN32)
    target_sources(platter-core PRIVATE
        platform/windows/win32_file_device.cpp
        platform/windows/win32_enumerate.cpp
    )
    target_link_libraries(platter-core PRIVATE setupapi)
    target_compile_definitions(platter-core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
else()
    target_sources(platter-core PRIVATE platform/posix/posix_file_device.cpp)
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
