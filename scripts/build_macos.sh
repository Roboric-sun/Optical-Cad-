#!/bin/zsh
set -euo pipefail
cd "${0:A:h}/.."
task_qt_root="${QT_ROOT:-/opt/homebrew}"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$task_qt_root"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/release/macOS"
print "Готово: $PWD/release/macOS/optical_cad.app"
