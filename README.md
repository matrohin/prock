# Prock

Process Explorer for Linux.

Built with Dear ImGui and ImPlot.

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
