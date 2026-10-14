#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/backend"
cmake -S . -B build
cmake --build build -j
exec ./build/extreamfs 8080
