cmake_minimum_required(VERSION 3.22)

# Single source of truth for the version: plugin metadata, and the tag the
# release workflow publishes.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WAIVES_VERSION)
string(STRIP "${WAIVES_VERSION}" WAIVES_VERSION)

project(Waives VERSION ${WAIVES_VERSION} LANGUAGES C CXX)

# JUCE's CMake support looks pkg-config up itself; doing it here first turns a
# cryptic failure inside JUCE into an actionable message.
if(UNIX AND NOT APPLE)
    find_program(PKG_CONFIG_EXECUTABLE pkg-config)
    if(NOT PKG_CONFIG_EXECUTABLE)
        message(FATAL_ERROR
            "pkg-config was not found, but JUCE requires it on Linux.\n"
            "  Debian/Ubuntu : sudo apt install pkg-config\n"
            "  Fedora        : sudo dnf install pkgconf-pkg-config\n"
            "  Arch          : sudo pacman -S pkgconf")
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WAIVES_BUILD_CLAP  "Build the CLAP format" ON)
option(WAIVES_BUILD_TESTS "Build the headless processor tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/WaivesPlugin.cmake)

enable_testing()

# One directory per plugin under plugins/. Only the plugins listed here are
# configured; add the directory name to build a new plugin.
set(WAIVES_PLUGINS
    MyFirstMasteringEQ)

foreach(plugin IN LISTS WAIVES_PLUGINS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${plugin}/CMakeLists.txt")
        message(FATAL_ERROR
            "plugins/${plugin}/CMakeLists.txt is missing, but ${plugin} is listed in WAIVES_PLUGINS")
    endif()

    add_subdirectory(plugins/${plugin})
endforeach()
