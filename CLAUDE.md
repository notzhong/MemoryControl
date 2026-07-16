# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Development Commands

The project uses a VSCode task that builds with MinGW g++ (from msys64 ucrt64). The build command from `.vscode/tasks.json`:

```bash
# Build all .cpp files into an executable
g++ -fdiagnostics-color=always -g *.cpp -o MemoryControl.exe
```

Since there are no test files or formal test framework, run the program directly to verify:

```bash
# Run the built executable
./MemoryControl.exe
```

**Note:** The compiler path is `G:\msys64\ucrt64\bin\g++.exe` (Windows MinGW). Adjust the path if using a different toolchain.

## Code Architecture

This is a **C++ memory management / leak detection tool** designed for debugging. It wraps `new[]` and `calloc` allocations to track all allocated memory, then reports any unreleased memory when the program exits.

### Core Components

**`memorycontrol.hpp`** - Header with three key elements:

1. **`memory_info` struct** - Metadata for each allocation: source file name, line number, and allocation size.
2. **`memorycontrol` class** (singleton) - Two allocation templates:
   - `new_memory<T>()` - Allocates via `new char[nSize]{0}` (zero-initialized)
   - `malloc_memory<T>()` - Allocates via `calloc(nSize, sizeof(char))`
3. **Macros** for ergonomic use:
   - `NEW_MEMORY(t)` → `getInstance()->new_memory<t>(sizeof(t), __FILE__, __LINE__)`
   - `DELETE_MEMORY(p)` → `getInstance()->delete_memory(p)`

**`memorycontrol.cpp`** - Singleton implementation:
- `getInstance()` - Returns the static singleton instance
- `delete_memory()` - Releases a tracked pointer, returns 0 on success, 1 if pointer not found, 2 if erase fails
- `print_no_release()` - Iterates `m_mc` map and prints all unreleased allocations

**`main.cpp`** - Example usage demonstrating how to initialize the tool and use the macros.

### Key Design Details

- **Tracking mechanism**: A static `std::map<void*, memory_info> m_mc` maps pointer addresses to their allocation metadata.
- **Singleton lifetime**: Create a global `memorycontrol` object (via the `init()` pattern in `main.cpp`) so its destructor runs at program exit, triggering leak detection.
- **Allocation scheme**: Both templates allocate raw `char` arrays, then `reinterpret_cast` to the desired type `T*`. The caller must manually construct objects (placement new) if needed.
- **All known issue**: The copy constructor parameter for `memory_info` is non-const (`memory_info(memory_info &other)`) which prevents binding to temporaries — likely a bug.
- **Typo**: `flie_name` in the struct should be `file_name` (named inconsistently with `file` parameter).
