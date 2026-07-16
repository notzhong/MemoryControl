# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Structure

```
MemoryControl/
├── CMakeLists.txt         ← Build system (library + demo)
├── memorycontrol.hpp      ← Public header (all macros, templates, RAII)
├── memorycontrol.cpp      ← Singleton implementation (.cpp = only source file)
├── main.cpp               ← Test / demo executable (not part of the library)
├── CLAUDE.md
└── .gitignore
```

**`memorycontrol` is a library.** `main.cpp` is only a test/demo program and is NOT part of the library itself.

## Build & Development Commands

```bash
# Configure + build library + demo (static lib by default)
cmake -B build && cmake --build build
./build/mc_demo

# Build as shared library
cmake -B build -DMEMORYCONTROL_BUILD_SHARED=ON && cmake --build build

# Build only the library (skip demo)
cmake -B build -DMEMORYCONTROL_BUILD_TESTS=OFF && cmake --build build

# Install (system-wide)
cmake --install build

# Quick test with g++ (no CMake needed)
g++ -std=c++11 -g -Wall -Wextra main.cpp memorycontrol.cpp -o mc_demo && ./mc_demo
```

## How to use the library in another project

### Via CMake (after install)

```cmake
find_package(MemoryControl REQUIRED)
target_link_libraries(your_target PRIVATE mc::memorycontrol)
```

### Via CMake (subdirectory)

```cmake
add_subdirectory(path/to/MemoryControl)
target_link_libraries(your_target PRIVATE memorycontrol)
```

### Via raw g++

```bash
g++ -std=c++11 -I/path/to/MemoryControl your_prog.cpp /path/to/MemoryControl/memorycontrol.cpp -o your_prog
```

## Disable tracking for release builds

Define `MEMORYCONTROL_DISABLE` before including the header, or link against `memorycontrol_rt`:

```cmake
# CMake: use the release-variant target
target_link_libraries(your_target PRIVATE memorycontrol_rt)
target_compile_definitions(your_target PRIVATE MEMORYCONTROL_DISABLE)
```

```bash
# g++
g++ -DMEMORYCONTROL_DISABLE -std=c++11 -I/path/to/MemoryControl your_prog.cpp \
    /path/to/MemoryControl/memorycontrol.cpp -o your_prog
```

When `MEMORYCONTROL_DISABLE` is defined all tracking macros become no-ops, and the `.cpp` file must still be compiled (its destructor will find an empty map and do nothing).

## Code Architecture

This is a **C++ memory management / leak detection library** for debugging. It wraps `new`, `new[]` and `calloc` allocations to track all allocated memory, then reports any unreleased memory when the program exits, including a leak summary with total count and byte count.

### Core Components

**`memorycontrol.hpp`** - Public header (the only file users include):

1. **`memory_info` struct** - Metadata for each allocation: source file, line number, size, and allocation type.
2. **`memorycontrol` class** (singleton) - Three allocation templates:
   - `new_single<T>()` — Allocates via `new (std::nothrow) T()` (calls constructor)
   - `new_memory<T>()` — Allocates via `new char[nSize]{0}` (zero-initialized raw memory)
   - `malloc_memory<T>()` — Allocates via `calloc(1, nSize)`
3. **`MemoryControlPtr<T>`** - Movable RAII smart pointer that auto-calls `DELETE_MEMORY` in its destructor.
4. **Compile-time switch** `MEMORYCONTROL_DISABLE` — when defined, all macros become no-ops.
5. **Convenience macros** (see table below).

**`memorycontrol.cpp`** - Singleton implementation (the only `.cpp` file):
- `getInstance()` — Returns the static singleton instance (C++11 thread-safe static local).
- `delete_memory(void *p)` — Releases a tracked pointer using the correct deallocator (`::operator delete`, `delete[]`, or `free()`). Returns 0 success / 1 not found / 2 erase failed.
- `print_no_release()` — Prints a leak summary (count + total bytes) then details of every unfreed allocation.

### Macros Provided

| Macro | Allocation method | Deallocated with |
|-------|-------------------|------------------|
| `NEW_SINGLE(t)` | `new (std::nothrow) t` (calls constructor) | `::operator delete` |
| `NEW_MEMORY(t)` | `new char[sizeof(t)]{0}` (zero-init, no ctor) | `delete[]` |
| `NEW_ARRAY(t, count)` | `new char[sizeof(t)*count]{0}` (zero-init) | `delete[]` |
| `CALLOC_MEMORY(t)` | `calloc(1, sizeof(t))` | `free()` |
| `CALLOC_ARRAY(t, count)` | `calloc(count, sizeof(t))` | `free()` |
| `DELETE_MEMORY(p)` | Frees any tracked pointer; also sets `p = nullptr` | auto-selected |

### Important Notes

- **NEW_SINGLE destructor**: Since `DELETE_MEMORY` does not know `T` at free time, it uses `::operator delete(p)` which does **not** call `~T()`. For non-trivial types call `p->~T()` manually **before** `DELETE_MEMORY(p)`.
- **Thread safety**: `std::mutex` protects the global map in all allocation/release operations.
- **Singleton lifetime**: The static local instance destructor runs automatically at program exit (C++11 guarantee).
- **Single `.cpp`**: Only `memorycontrol.cpp` needs to be compiled and linked; the entire API is in the header.

### Known Issues

All issues from the original version have been resolved. The project compiles cleanly with `-Wall -Wextra` at C++11 and above.