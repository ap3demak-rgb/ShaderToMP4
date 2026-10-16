#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET
# Archive portable Linux : dist/ShaderToMP4-<version>-linux-<arch>.tar.gz (un dossier, aucun installeur).
# Usage : ./scripts/package-linux.sh x86_64|aarch64   (après ./scripts/build.sh)
set -euo pipefail
cd "$(dirname "$0")/.."
arch="${1:?architecture : x86_64 ou aarch64}"
version="$(sed -n 's/^#define APP_VER_STR "\(.*\)"/\1/p' src/app_info.h)"
name="ShaderToMP4-${version}-linux-${arch}"
rm -rf "dist/${name}"
mkdir -p "dist/${name}/lang"
cp bin/ShaderToMP4 "dist/${name}/"
bin/ShaderToMP4 --export-logo 256 "dist/${name}/ShaderToMP4.png"
cp COPYING LICENSE THIRD_PARTY_NOTICES.md THIRD_PARTY_NOTICES.fr.md README.md README.fr.md "dist/${name}/"
cp lang/*.json "dist/${name}/lang/"
mkdir -p "dist/${name}/transitions" "dist/${name}/examples/effects"
cp transitions/*.glsl "dist/${name}/transitions/"
cp examples/effects/*.glsl "dist/${name}/examples/effects/"
tar -C dist -czf "dist/${name}.tar.gz" "${name}"
echo "dist/${name}.tar.gz"
