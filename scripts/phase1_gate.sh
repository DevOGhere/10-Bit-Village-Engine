#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."
mkdir -p build
cd build

echo "--- BUILDING ENGINE + LLAMA.CPP ---"
cmake ..
make -j4

cd ..
echo "--- RUNNING PHASE 1 GATE ---"
./build/tbv_engine --phase1
