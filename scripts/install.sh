#!/usr/bin/sh

set -e

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$REPO_DIR"

cmake --preset release
cmake --build ./build/Release --target prock

sudo cmake --install ./build/Release
sudo cp prock.desktop /usr/share/applications/
