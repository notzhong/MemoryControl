cmake_minimum_required(VERSION 3.10)
project(MemoryControl VERSION 2.0.0 LANGUAGES CXX)

# ── C++ standard ────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Options ─────────────────────────────────────────────────────────────────
option(MEMORYCONTROL_BUILD_SHARED  "Build as shared library"      OFF)
option(MEMORYCONTROL_BUILD_TESTS   "Build test/demo executable"    ON)
option(MEMORYCONTROL_GLOBAL_HOOK   "Override global operator new/delete" OFF)

# ── Library type ────────────────────────────────────────────────────────────
if(MEMORYCONTROL_BUILD_SHARED)
    set(MC_LIB_TYPE SHARED)
else()
    set(MC_LIB_TYPE STATIC)
endif()

# ── Compiler-specific settings ─────────────────────────────────────────────
if(MSVC)
    # MSVC: disable warnings about POSIX name deprecation, enable debug symbols
    add_compile_options(/W3 /wd4996 /Zi)
    # Avoid "__cplusplus" macro issues with MSVC
    set(CMAKE_CXX_STANDARD 17)
else()
    # GCC / Clang
    add_compile_options(-Wall -Wextra -g)
endif()

# ── Platform-specific link dependencies ─────────────────────────────────────
if(WIN32)
    # Windows: dbghelp.lib is needed for SymFromAddr / SymGetLineFromAddr64
    set(PLATFORM_LIBS dbghelp)
elseif(APPLE)
    # macOS: execinfo is in a separate library
    set(PLATFORM_LIBS execinfo)
else()
    # Linux: backtrace is in glibc — no extra lib needed
    set(PLATFORM_LIBS)
endif()

# ── Library: tracking enabled ──────────────────────────────────────────────
add_library(memorycontrol ${MC_LIB_TYPE}
    memorycontrol.cpp
)
target_include_directories(memorycontrol
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include>
)
target_link_libraries(memorycontrol PRIVATE ${PLATFORM_LIBS})

if(MEMORYCONTROL_BUILD_SHARED AND WIN32)
    target_compile_definitions(memorycontrol PRIVATE MEMORYCONTROL_EXPORTS)
endif()

if(MEMORYCONTROL_GLOBAL_HOOK)
    target_compile_definitions(memorycontrol PRIVATE MEMORYCONTROL_GLOBAL_HOOK)
endif()

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

    if(WIN32)
        # On Windows, the demo executable also needs dbghelp for runtime symbols
        target_link_libraries(mc_demo PRIVATE dbghelp)
    endif()
endif()

# ── Install ─────────────────────────────────────────────────────────────────
if(NOT WIN32)
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
else()
    # Windows install (simplified)
    install(TARGETS memorycontrol
        EXPORT  MemoryControlTargets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
    install(FILES memorycontrol.hpp DESTINATION include)
    install(EXPORT MemoryControlTargets
        FILE    MemoryControlTargets.cmake
        NAMESPACE mc::
        DESTINATION lib/cmake/MemoryControl
    )
endif()