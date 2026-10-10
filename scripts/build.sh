#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET
# Compile bin/ShaderToMP4 sous Linux ou macOS (CMake + un compilateur C++17). Usage : ./scripts/build.sh
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build-cmake --parallel
