# Prock

Process Explorer for Linux.

The tool is still in development. I need it myself to improve my Linux desktop experience.

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

**Debian/Ubuntu:**
```bash
sudo apt install cmake gcc libwayland-dev libxkbcommon-dev xorg-dev libgles2-mesa-dev
```

**Arch Linux:**
```bash
sudo pacman -Sy cmake gcc
```

> **Note for GNOME users:** If the window title bar is not displayed on Wayland, this may be due to a [known mutter issue](https://gitlab.gnome.org/GNOME/mutter/-/issues/217). Use the `release-x11` preset instead to disable Wayland support.

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
