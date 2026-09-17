cmake_minimum_required(VERSION 4.0)

if(CMAKE_VERSION VERSION_LESS 4.1)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "a9e1cf81-9932-4810-974b-6eccaf14e457")
elseif(CMAKE_VERSION VERSION_LESS 4.3)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
elseif(CMAKE_VERSION VERSION_LESS 4.4)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
elseif(CMAKE_VERSION VERSION_LESS 4.5)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
else()
    message(FATAL_ERROR
        "CMake ${CMAKE_VERSION} is newer than the `import std` gate table in this file. "
        "Take the activation value from the `import std` section of "
        "Help/dev/experimental.rst shipped with this CMake and add a branch above.")
endif()

project(voxelworld LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

if(NOT CMAKE_GENERATOR MATCHES "Ninja")
    message(FATAL_ERROR
        "Generator '${CMAKE_GENERATOR}' cannot build C++ modules with `import std`; "
        "configure with -G Ninja")
endif()

include(vw_std_module)
include(vw_warnings)
include(vw_vulkan_module)

option(VW_BUILD_GFX "Build the Vulkan renderer and everything that needs a GPU" ON)
option(VW_BUILD_APPS "Build applications" ON)
option(VW_BUILD_TESTS "Build unit tests" ON)

option(VW_BUILD_FUZZERS "Build libFuzzer targets for the asset parsers" OFF)

if(VW_BUILD_FUZZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "VW_BUILD_FUZZERS needs Clang: libFuzzer ships with it and nothing else")
endif()

if(VW_BUILD_APPS AND NOT VW_BUILD_GFX)
    message(FATAL_ERROR "VW_BUILD_APPS needs VW_BUILD_GFX: every app opens a window")
endif()

enable_testing()

add_subdirectory(engine)

if(VW_BUILD_APPS)
    add_subdirectory(apps)
endif()

if(VW_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(VW_BUILD_FUZZERS)
    add_subdirectory(tests/fuzz)
endif()
