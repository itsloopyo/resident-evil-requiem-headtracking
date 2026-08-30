cmake_minimum_required(VERSION 3.20)
project(RE9HeadTracking VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# cameraunlock-core shared library (static) with REFramework utils.
# No hook manager: this plugin detours nothing natively - every hook it
# installs goes through REFramework's own plugin API - so CAMERAUNLOCK_BUILD_HOOKS
# would only pull MinHook off GitHub at configure time and break a firewalled
# build for a library nothing links against.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_REFRAMEWORK ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern" CACHE STRING "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Plugin sources
add_library(RE9HeadTracking SHARED
    src/pch.cpp
    src/plugin_main.cpp
    src/camera/aim_trace.cpp
    src/camera/crosshair.cpp
    src/camera/flashlight_hook.cpp
    src/camera/gui_compensation.cpp
    src/camera/game_state_detector.cpp
)

target_precompile_headers(RE9HeadTracking PRIVATE src/pch.h)

set_target_properties(RE9HeadTracking PROPERTIES
    OUTPUT_NAME "RE9HeadTracking"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(RE9HeadTracking PRIVATE
    cameraunlock cameraunlock_reframework ws2_32)

target_include_directories(RE9HeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern
)
