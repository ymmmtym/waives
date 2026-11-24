cmake_minimum_required(VERSION 3.22)

project(Waives VERSION 0.1.0 LANGUAGES C CXX)

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
