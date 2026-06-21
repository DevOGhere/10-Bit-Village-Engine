#!/usr/bin/env bash
# Phase 3 determinism gate. Builds via CMake (links llama + bridge + world) and runs the
# in-binary --phase3 gate, which hashes MemoryGraph + positions + needs and asserts
# same-seed byte-identity + diff-seed divergence + mythology emergence.
set -e
cd "$(dirname "$0")/.."
cmake --build build --target tbv_engine
./build/tbv_engine --phase3 "${1:-200}"
