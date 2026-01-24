# Prock

Process Explorer for Linux.

## Features

### Process Monitoring
- Flat list or tree view showing parent-child relationships
- Filter processes by name (Ctrl+F)
- Sortable and reorderable columns
- Copy process info or entire table to clipboard
- Kill (SIGTERM) or force kill (SIGKILL) processes

### System Charts
- CPU usage with optional per-core and stacked views
- Memory usage (used vs available)
- Disk I/O throughput (read/write MB/s)
- Network throughput (send/receive MB/s)

### Per-Process Details
- Right-click any process to open:
  - Dedicated charts (CPU, memory, I/O, network)
  - Loaded libraries with mapped/file sizes
  - Environment variables
  - Process threads
- Double-click to open all windows at once

## Building

### Dependencies

```bash
sudo apt install libwayland-dev libxkbcommon-dev xorg-dev libgles2-mesa-dev
```

### Release build & Install

```bash
cmake --preset release
cmake --build ./build/Release
sudo cmake --install ./build/Release
```

### Debug build & Run

```bash
cmake --preset debug
./build.sh
./build/Debug/prock
```
