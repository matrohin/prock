# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

This is a CMake-based C++ project targeting Linux with OpenGL ES 2.0. **All build commands must be run via WSL.**

```bash
# Configure (from project root, via WSL)
wsl cmake -B build/Debug -DCMAKE_BUILD_TYPE=Debug
wsl cmake -B build/Release -DCMAKE_BUILD_TYPE=Release

# Build (via WSL)
wsl cmake --build build/Debug
wsl cmake --build build/Release

# Run (via WSL)
wsl ./build/Debug/prock
wsl ./build/Release/prock
```

### Dependencies (Linux)
```bash
sudo apt install exuberant-ctags libwayland-dev libxkbcommon-dev xorg-dev libgles2-mesa-dev cmake-mozilla
```

## Architecture

**prock** is a Linux process monitor GUI application built with ImGui, ImPlot, and GLFW.

### Unity Build
The project uses a unity build pattern. `main.cpp` includes all application `.cpp` files directly, and `imgui_all.cpp` bundles ImGui/ImPlot sources. Do not add source files to CMakeLists.txt - include them in `main.cpp` instead.

### Threading Model
- **Main thread**: Handles GLFW window events, ImGui rendering, and state updates
- **Gathering thread**: Reads `/proc` filesystem data and pushes snapshots via lock-free queue

### Key Components

- `src/base.h` - Core types: `BumpArena` (bump allocator), `Array`, `GrowingArray`, `LinkedList`
- `src/process_stat.h` - `ProcessStat` struct matching `/proc/[pid]/stat` and `/proc/[pid]/statm` fields
- `src/state.h` - `State` (main app state), `StateSnapshot` (per-update process data), `ProcessDerivedStat` (computed metrics like CPU%)
- `src/sync.h` - `Sync` struct with atomic quit flag and `RingBuffer` for thread communication
- `src/ring_buffer.h` - Lock-free SPSC ring buffer for inter-thread updates

### Views (src/views/)
Each view has update and draw functions called from `views_update()` and `views_draw()`:
- `brief_table` - Compact process list
- `full_table` - Detailed process table
- `cpu_chart` - CPU usage chart (ImPlot)
- `mem_chart` - Memory usage chart (ImPlot)

### Memory Management
Uses arena allocation (`BumpArena`) for per-frame data. The `snapshot_arena` is destroyed after each update cycle, so snapshot data is transient.

## Coding Guidelines

- **No C++ stdlib containers** - Do not use `std::vector`, `std::map`, `std::string`, etc. Use the custom types in `base.h`: `Array`, `GrowingArray`, `LinkedList`
- **Arena allocation only** - All dynamic allocations must go through `BumpArena`. No raw `new`/`malloc` outside of arena internals
- **Only modify `src/`** - Do not modify vendored libraries (`imgui/`, `implot/`, `glfw-3.4/`) or build configuration
