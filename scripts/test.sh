#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET
# Auto-test : rend les shaders de tests/ et compare aux images de reference. Usage : ./scripts/test.sh [--update]
set -euo pipefail
cd "$(dirname "$0")/.."
exec bin/ShaderToMP4 --selftest --quiet "$@"
