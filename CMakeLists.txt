cmake_minimum_required(VERSION 3.20)

project(ETLFragFinder VERSION 1.7.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0601 UNICODE _UNICODE)
endif()

add_library(etl_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(etl_sqlite PUBLIC third_party/sqlite)
target_compile_definitions(
    etl_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)

add_library(etl_demo_parser STATIC
    src/etl_demo_parser.cpp
    src/idtech3_huffman.cpp
    src/app_storage.cpp
)
target_include_directories(etl_demo_parser PUBLIC src)
target_link_libraries(etl_demo_parser PUBLIC etl_sqlite)
if(UNIX AND NOT APPLE)
    target_link_libraries(etl_sqlite PUBLIC dl pthread)
endif()

if(MSVC)
    target_compile_options(etl_demo_parser PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(etl_demo_parser PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(etl-frag-cli src/main_cli.cpp)
target_link_libraries(etl-frag-cli PRIVATE etl_demo_parser)

if(WIN32)
    add_executable(ETLFragFinder WIN32 src/win_main.cpp src/app.rc)
    target_link_libraries(
        ETLFragFinder PRIVATE etl_demo_parser comctl32 comdlg32 shell32 ole32 uuid gdi32 dwmapi uxtheme)
    if(MSVC)
        target_compile_options(ETLFragFinder PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

install(TARGETS etl-frag-cli RUNTIME DESTINATION .)
if(WIN32)
    install(TARGETS ETLFragFinder RUNTIME DESTINATION .)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(etl-frag-tests tests/test_frag_runs.cpp)
    target_link_libraries(etl-frag-tests PRIVATE etl_demo_parser)
    add_test(NAME frag-runs COMMAND etl-frag-tests)
endif()
