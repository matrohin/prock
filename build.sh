#!/usr/bin/sh

cmake --build build/Debug
ctest --test-dir build/Debug
