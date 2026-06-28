#!/usr/bin/sh

find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
cmake --build build/Debug
./build/Debug/prock_tests
