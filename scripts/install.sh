#!/usr/bin/sh

cmake --preset release
cmake --build ./build/Release

sudo cmake --install ./build/Release
sudo cp prock.desktop /usr/share/applications/
