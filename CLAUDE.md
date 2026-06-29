# CLAUDE.md

## Build

```bash
cmake --preset debug
./scripts/build.sh
```

## Architecture

Linux process monitor GUI built with ImGui, ImPlot, and GLFW (OpenGL ES 2.0).

### Unity Build
`main.cpp` includes all application `.cpp` files directly. `imgui_all.cpp` is a separate static library for ImGui/ImPlot sources. Do not add source files to CMakeLists.txt - include them in `main.cpp` instead.

### Threading
- **Main thread**: GLFW events, ImGui rendering, state updates
- **Gathering thread**: Reads `/proc`, pushes snapshots via lock-free SPSC ring buffer
- **Library Loader thread**: Reads `/proc/<pid>/maps` on demand from main thread

### Key Types
- `src/base.h` - `BumpArena`, `Array`, `GrowingArray`, `LinkedList`
- `src/state.h` - `State`, `StateSnapshot`, `ProcessDerivedStat`
- `src/process_stat.h` - `ProcessStat` (mirrors `/proc/[pid]/stat` and `/proc/[pid]/statm`)
- `src/sync.h` - `Sync` (atomic quit flag + `RingBuffer`)

### Views (`src/views/`)
Each view has update and draw functions called from `views_update()` and `views_draw()`:
- `brief_table` - Process list with tree view, filtering, type-to-search
- `cpu_chart`, `mem_chart`, `io_chart`, `net_chart` - Per-process charts (ImPlot)
- `system_cpu_chart`, `system_mem_chart`, `system_io_chart`, `system_net_chart` - System-wide charts
- `threads_viewer`, `socket_viewer`, `environ_viewer`, `library_viewer` - Per-process inspectors

### Memory
Arena allocation (`BumpArena`) for per-frame data. `snapshot_arena` is destroyed after each update cycle.

## Coding Guidelines

- Use custom containers from `base.h` (`Array`, `GrowingArray`, `LinkedList`)
- All dynamic allocations through `BumpArena` - no raw `new`/`malloc`
- To build formatted strings (e.g. `/proc/<pid>/...` paths), use `String::sprintf(temp_arena, ...)` instead of local `char buf[N]` + `snprintf`. Keep fixed local buffers only for syscall output (e.g. `readlink`, `fgets`)
- Only modify `src/` - do not touch vendored libraries (`imgui/`, `implot/`, `glfw-3.4/`) or build configuration
- Do not add co-authored-by to commits
