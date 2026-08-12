cmake_minimum_required(VERSION 3.20)
project(AlienIsolationHeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "Alien: Isolation is Windows-only.")
endif()

# AI.exe is a 32-bit PE; the .asi must match its bitness. Configure with
# `cmake -A Win32 ...` (pixi handles this).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Must configure for 32-bit. Use: cmake -S . -B build -A Win32")
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
# Static CRT so the .asi has no MSVC redist dependency on the user's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# OFF builds the loader-confirmation variant: logger + UDP + hotkeys only, so a
# crash can't be confused with a bad hook. The loader is confirmed, so the
# camera hook is the default; a build without it has no head tracking at all.
option(AIHT_BUILD_CAMERA "Build the camera hook (requires MinHook)" ON)

# RE instrumentation: a full address-space scan, a vectored exception handler
# and hardware data breakpoints armed on every thread in the game. Useful for
# deriving offsets on a new build, never acceptable in something a player runs.
option(AIHT_BUILD_CAMERA_SCAN "Build the reverse-engineering memory scanner" OFF)

# --- cameraunlock-core static library -------------------------------------
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(AIHT_BUILD_CAMERA)
    include(FetchContent)
    # Pinned to a commit, not a branch: this DLL is injected into the player's
    # game process, so the bytes that end up in a release must be the bytes
    # that were reviewed. A branch ref also makes CI and a developer's machine
    # build different binaries from the same repo commit.
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG        d94c64d32ea37bc4f5ee47d580709f70c6fb6080  # master, 2026-06-13
    )
    FetchContent_MakeAvailable(minhook)
    set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
else()
    set(CAMERAUNLOCK_BUILD_HOOKS OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# --- mod plugin (.asi) ----------------------------------------------------
set(AIHT_SOURCES
    src/AlienIsolationHeadTracking/config.cpp
    src/AlienIsolationHeadTracking/dllmain.cpp
)
if(AIHT_BUILD_CAMERA)
    list(APPEND AIHT_SOURCES
        src/AlienIsolationHeadTracking/camera_hook.cpp
        src/AlienIsolationHeadTracking/camera_matrix.cpp
        src/AlienIsolationHeadTracking/constant_buffer_injection.cpp
        src/AlienIsolationHeadTracking/engine_camera_hooks.cpp
        src/AlienIsolationHeadTracking/head_transform.cpp
        src/AlienIsolationHeadTracking/hud_prompt.cpp
        src/AlienIsolationHeadTracking/injection_state.cpp
        src/AlienIsolationHeadTracking/intro_skip.cpp
    )
    if(AIHT_BUILD_CAMERA_SCAN)
        list(APPEND AIHT_SOURCES src/AlienIsolationHeadTracking/camera_scan.cpp)
    endif()
endif()

add_library(AlienIsolationHeadTracking SHARED ${AIHT_SOURCES})

target_include_directories(AlienIsolationHeadTracking PRIVATE
    src/AlienIsolationHeadTracking
)
target_link_libraries(AlienIsolationHeadTracking PRIVATE cameraunlock)

if(AIHT_BUILD_CAMERA)
    target_link_libraries(AlienIsolationHeadTracking PRIVATE
        cameraunlock_hooks minhook
        d3d11 dxgi d3dcompiler
    )
    target_compile_definitions(AlienIsolationHeadTracking PRIVATE AIHT_CAMERA=1)
    if(AIHT_BUILD_CAMERA_SCAN)
        target_compile_definitions(AlienIsolationHeadTracking PRIVATE AIHT_CAMERA_SCAN=1)
        message(WARNING "AIHT_BUILD_CAMERA_SCAN is ON. This build arms hardware "
                        "breakpoints and scans process memory. Do not ship it.")
    endif()
endif()

set_target_properties(AlienIsolationHeadTracking PROPERTIES
    OUTPUT_NAME "AlienIsolationHeadTracking"
    PREFIX ""
    SUFFIX ".asi"
)

# --- tests ----------------------------------------------------------------
# Covers the parts of the mod that are pure logic and can run off a game
# process. Built with the mod, and `pixi run package` depends on `pixi run
# test`, so a failing test cannot reach a release.
option(AIHT_BUILD_TESTS "Build unit tests" ON)
if(AIHT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
