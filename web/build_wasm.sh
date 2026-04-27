#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v emcc >/dev/null 2>&1; then
    if [ -f /tmp/emsdk/emsdk_env.sh ]; then
        # shellcheck disable=SC1091
        source /tmp/emsdk/emsdk_env.sh >/dev/null
    fi
fi

if ! command -v emcc >/dev/null 2>&1; then
    echo "emcc was not found. Install Emscripten or source emsdk_env.sh first." >&2
    exit 1
fi

mkdir -p "$ROOT/web/public"

emcc "$ROOT/web/src/simulator_wasm.cpp" \
    -std=c++17 \
    -O3 \
    -s MODULARIZE=1 \
    -s EXPORT_ES6=1 \
    -s EXPORT_NAME=createBgpSimulatorModule \
    -s ENVIRONMENT=web,node \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_simulate_target","_free_result","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8"]' \
    -o "$ROOT/web/public/simulator.js"

echo "Built web/public/simulator.js and web/public/simulator.wasm"
