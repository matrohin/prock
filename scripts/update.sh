#!/usr/bin/sh

git pull

cmake --build ./build/Release
sudo cmake --install ./build/Release
