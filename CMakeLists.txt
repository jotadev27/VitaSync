cmake_minimum_required(VERSION 3.21)

project(VitaSync
    VERSION 1.0.0
    DESCRIPTION "Local-network companion suite for a jailbroken PS Vita"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(VSP_BUILD_TESTS "Build the core unit tests" ON)

# The core is deliberately GUI-free so the exact same object code backs the
# desktop binary and the Android package.
# Multimedia is optional: with it, a dropped video previews a real decoded
# frame; without it, videos get a drawn plate and everything else is unchanged.
# Declared here rather than in media/ so Qt's target finalisation can see it.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Concurrent Quick QuickControls2
             OPTIONAL_COMPONENTS Multimedia)

qt_standard_project_setup(REQUIRES 6.5)

add_subdirectory(core)
add_subdirectory(media)
add_subdirectory(desktop)

if(VSP_BUILD_TESTS AND NOT ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(core/tests)
endif()
