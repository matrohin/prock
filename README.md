# Prock - Process Explorer for Linux

[![CI](https://github.com/matrohin/prock/actions/workflows/ci.yml/badge.svg)](https://github.com/matrohin/prock/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Prock is a fast, graphical alternative to htop/btop, inspired by the
Sysinternals Process Explorer. It shows a process tree, live system charts,
per-process charts, and detailed inspectors for libraries, sockets, threads,
and environment — with controls to suspend, kill, or reprioritize processes.

![Prock per-process windows and ports view (Light theme)](./images/shot-light.png)

![Prock process tree with live system charts (Nord theme)](./images/shot-nord.png)

## Features

### Process Monitoring
- Filter by name, PID; Use '+' to filter to a subtree
- Sort by any column
- Choose the order and which columns to display
- Copy process info or entire table
- Kill `SIGTERM`, force kill `SIGKILL`, or kill entire process tree
- Tree view or a sortable list showing parent-child relationships
- Set CPU affinity and nice priority
- Suspend `SIGSTOP` / resume `SIGCONT` processes
- Create a core dump (via `gcore`) without terminating the process

### System Charts
- CPU usage (total, kernel, interrupts)
- Toggle to show per-core and stacked charts (per-core toggles the format for all views globally)
- Memory usage (used vs available)
- Disk I/O throughput (read/write MB/s)
- Network throughput (send/receive MB/s)

### Per-Process Details
- Right-click any process to open:
  - Charts (CPU, memory, I/O)
  - Properties
  - Loaded libraries with mapped/file sizes
  - Memory maps with address, permissions, RSS/PSS/swap, and grouping by mapping
  - Sockets
  - Threads
  - Open files (fd, type, access, size, path)
  - Environment variables
- Double-click to open all windows at once

### Listening Ports
- System-wide view of listening ports
- Kill the owning process directly from the table

### Charts & Navigation
- Toggle "Auto-Follow" on all charts and tables (Space)
- Toggle continuous Y-axis auto-fit (Shift+Space)
- Per-core and stacked CPU chart views
- Zoom the UI in / out (Ctrl++ / Ctrl+-)
- Command palette for all actions (Ctrl+P)
- Context menu for the selected table row (Ctrl+Enter)
- Menu bar, optionally shown only on Alt

### Appearance & Preferences
- Themes: Dark, Light, Classic, Enemymouse, Nord, OneNord, Everforest Dark, Everforest Light
- Custom TTF font
- Adjustable window opacity, UI scale, target FPS, and update interval

## Runtime Dependencies

The released `prock` binary targets glibc 2.17+, so it runs on most Linux distros.
It dynamically loads the libraries below, which are already present on a standard desktop install:
- **glibc 2.17+** — `libc`, `libm`, `libpthread`, `libdl`
- **OpenGL ES 2.0 + EGL** — `libGLESv2.so.2`, `libEGL.so.1`
- **FreeType** — `libfreetype.so.6`
- **A display backend**, one of:
  - **X11** — `libX11.so.6` `libXcursor`, `libXi`, `libXrandr`, `libXinerama`
  - **Wayland** — `libwayland-client.so.0`, `libwayland-cursor.so.0`, `libwayland-egl.so.1`, `libxkbcommon.so.0`

Optionally, the "Create Dump File" action shells out to `gcore` (shipped with
**gdb**); install gdb to enable it.

On a minimal or headless system, install them explicitly:

**Debian/Ubuntu:**
```bash
sudo apt install libgles2 libegl1 libfreetype6 libx11-6 libxcursor1 libxi6 libxrandr2 libxinerama1 libwayland-client0 libwayland-cursor0 libwayland-egl1 libxkbcommon0
```

**Arch Linux:**
```bash
sudo pacman -S mesa libglvnd freetype2 libx11 libxcursor libxi libxrandr libxinerama wayland libxkbcommon
```

## Building

### Dependencies

**Debian/Ubuntu:**
```bash
sudo apt install cmake gcc libwayland-dev libxkbcommon-dev xorg-dev libgles2-mesa-dev libfreetype-dev
```

**Arch Linux:**
```bash
sudo pacman -Sy cmake gcc freetype2
```

### Install & Update

Install:
```bash
git clone https://github.com/matrohin/prock.git
cd prock

./scripts/install.sh
```

Update:
```bash
cd prock

./scripts/update.sh
```

### Debug build & Run

```bash
cmake --preset debug
./scripts/build.sh
./build/Debug/prock
```
