# Vendored SQLite amalgamation (public domain). See sqlite3/README.md.
add_library(platter-sqlite3 STATIC sqlite3/sqlite3.c)
target_include_directories(platter-sqlite3 SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3)
target_compile_definitions(platter-sqlite3 PUBLIC
    SQLITE_DQS=0
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1)
set_target_properties(platter-sqlite3 PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(platter-sqlite3 PRIVATE /w)
else()
    target_compile_options(platter-sqlite3 PRIVATE -w)
endif()
find_package(Threads REQUIRED)
target_link_libraries(platter-sqlite3 PUBLIC Threads::Threads)
if(UNIX)
    target_link_libraries(platter-sqlite3 PUBLIC ${CMAKE_DL_LIBS} m)
endif()
add_library(SQLite::SQLite3 ALIAS platter-sqlite3)
