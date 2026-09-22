cmake_minimum_required(VERSION 3.24)
project(linux-update-tool VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Quick
    QuickControls2
    Qml
    DBus
    Network
    Svg
    Test
)

find_package(SQLite3 REQUIRED)
if(TARGET SQLite::SQLite3 AND NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()
if(TARGET SQLite3::SQLite3 AND NOT TARGET SQLite::SQLite3)
    add_library(SQLite::SQLite3 ALIAS SQLite3::SQLite3)
endif()
find_package(PolkitQt6-1 REQUIRED)
find_package(AppStreamQt REQUIRED)
find_package(KF6Service REQUIRED)
find_package(KF6KIO REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALPM libalpm)

enable_testing()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(liblut)
add_subdirectory(lutd)
add_subdirectory(linux-update-tool)
add_subdirectory(tests)
add_subdirectory(tools)
