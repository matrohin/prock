# Prock

Process Explorer for Linux.

The tool is still in development. I need it myself to improve my Linux desktop experience.

## Features

### Process Monitoring
- Tree view (default) or flat list showing parent-child relationships
- Filter by name (Ctrl+F) or type to jump to a process
- Sortable and reorderable columns
- Copy process info or entire table to clipboard
- Kill (SIGTERM), force kill (SIGKILL), or kill entire process tree
- Suspend / resume processes
- Set CPU affinity and nice priority

### System Charts
- CPU usage (total, kernel, interrupts) with optional per-core and stacked views
- Memory usage (used vs available)
- Disk I/O throughput (read/write MB/s)
- Network throughput (send/receive MB/s)

### Per-Process Details
- Right-click any process to open:
  - Dedicated charts (CPU, memory, I/O, network)
  - Loaded libraries with mapped/file sizes
  - Memory maps with address, permissions, RSS/PSS/swap, and grouping by mapping
  - Sockets
  - Threads
  - Environment variables
- Double-click to open all windows at once

## Building

### Dependencies

**Debian/Ubuntu:**
```bash
sudo apt install cmake gcc libwayland-dev libxkbcommon-dev xorg-dev libgles2-mesa-dev
```

**Arch Linux:**
```bash
sudo pacman -Sy cmake gcc
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

## Screenshots

My configuration:
* JetBrainsMonoNerdFontMono-Regular font.
* Nord Theme.

![Screenshot](./images/screenshot.png)
