cmake_minimum_required(VERSION 3.10)
project(MemoryControl VERSION 1.1.0 LANGUAGES CXX)

# ── C++ standard ────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Options ─────────────────────────────────────────────────────────────────
option(MEMORYCONTROL_BUILD_SHARED "Build as shared library" OFF)
option(MEMORYCONTROL_BUILD_TESTS  "Build test/demo executable"  ON)

# ── Library type ────────────────────────────────────────────────────────────
if(MEMORYCONTROL_BUILD_SHARED)
    set(MC_LIB_TYPE SHARED)
else()
    set(MC_LIB_TYPE STATIC)
endif()

# ── Library: tracking enabled (debug) ───────────────────────────────────────
add_library(memorycontrol ${MC_LIB_TYPE}
    memorycontrol.cpp
)
target_include_directories(memorycontrol
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include>
)

# ── Library: tracking disabled (release variant) ────────────────────────────
add_library(memorycontrol_rt ${MC_LIB_TYPE}
    EXCLUDE_FROM_ALL
    memorycontrol.cpp
)
target_include_directories(memorycontrol_rt
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include>
)
target_compile_definitions(memorycontrol_rt PRIVATE MEMORYCONTROL_DISABLE)

# ── Test / demo executable ──────────────────────────────────────────────────
if(MEMORYCONTROL_BUILD_TESTS)
    add_executable(mc_demo
        main.cpp
    )
    target_link_libraries(mc_demo PRIVATE memorycontrol)
endif()

# ── Install ─────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS memorycontrol
    EXPORT  MemoryControlTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES memorycontrol.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT MemoryControlTargets
    FILE    MemoryControlTargets.cmake
    NAMESPACE mc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MemoryControl
)