cmake_minimum_required(VERSION 3.21)

project(platter
    VERSION 1.1.3
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

if(WIN32)
    set(PLATTER_RUNTIME_DESTINATION app)
    set(PLATTER_DOC_DESTINATION docs)
else()
    set(PLATTER_RUNTIME_DESTINATION bin)
    set(PLATTER_DOC_DESTINATION share/doc/platter)
endif()

add_subdirectory(src)

if(PLATTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------ install
install(FILES README.md CHANGELOG.md LICENSE DESTINATION ${PLATTER_DOC_DESTINATION} COMPONENT Runtime)
install(DIRECTORY packaging/licenses/ DESTINATION ${PLATTER_DOC_DESTINATION}/licenses COMPONENT Runtime)
install(DIRECTORY docs/ DESTINATION ${PLATTER_DOC_DESTINATION} COMPONENT Runtime FILES_MATCHING PATTERN "*.md")
if(UNIX AND NOT APPLE)
    # pkexec needs the exact installed path of the helper in its policy, so the
    # file is generated at install time, when the real prefix is known.
    install(CODE "
        set(PLATTER_HELPER_INSTALLED_PATH \"\${CMAKE_INSTALL_PREFIX}/libexec/platter/platter-helper\")
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/share/polkit-1/actions\")
        configure_file(\"${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/org.platter.helper.policy.in\"
                       \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/share/polkit-1/actions/org.platter.helper.policy\" @ONLY)
    " COMPONENT Runtime)
    install(FILES packaging/linux/platter.desktop DESTINATION share/applications COMPONENT Runtime)
    install(FILES assets/platter.svg DESTINATION share/icons/hicolor/scalable/apps COMPONENT Runtime)
endif()

# ------------------------------------------------------------------- cpack
set(CPACK_PACKAGE_NAME "platter")
set(CPACK_PACKAGE_VENDOR "platter")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
# FetchContent dependencies may define their own headers/libraries install rules.
# Ship only the application component, keeping one portable archive per platform.
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_SET_DESTDIR ON)   # stage with DESTDIR so install-time code (the polkit policy) sees the real prefix; the package is rooted at CMAKE_INSTALL_PREFIX
endif()
include(CPack)
