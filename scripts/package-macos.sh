#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET
# Archive portable macOS (binaire universel arm64 + x86_64) : dist/ShaderToMP4-<version>-macos-universal.zip
# contenant ShaderToMP4.app (signature ad hoc, sans installeur ni dmg) et les fichiers qui l'accompagnent.
# Usage : ./scripts/package-macos.sh   (après ./scripts/build.sh -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64")
set -euo pipefail
cd "$(dirname "$0")/.."
version="$(sed -n 's/^#define APP_VER_STR "\(.*\)"/\1/p' src/app_info.h)"
name="ShaderToMP4-${version}-macos-universal"
root="dist/${name}"
app="${root}/ShaderToMP4.app"
rm -rf "${root}"
mkdir -p "${app}/Contents/MacOS" "${app}/Contents/Resources" "${root}/lang"
cp bin/ShaderToMP4 "${app}/Contents/MacOS/ShaderToMP4"

# Icône : le logo SVG embarqué est rastérisé par le programme lui-même
iconset="$(mktemp -d)/ShaderToMP4.iconset"
mkdir -p "${iconset}"
for size in 16 32 64 128 256 512 1024; do
    bin/ShaderToMP4 --export-logo "${size}" "${iconset}/s${size}.png"
done
cp "${iconset}/s16.png" "${iconset}/icon_16x16.png"
cp "${iconset}/s32.png" "${iconset}/icon_16x16@2x.png"
cp "${iconset}/s32.png" "${iconset}/icon_32x32.png"
cp "${iconset}/s64.png" "${iconset}/icon_32x32@2x.png"
cp "${iconset}/s128.png" "${iconset}/icon_128x128.png"
cp "${iconset}/s256.png" "${iconset}/icon_128x128@2x.png"
cp "${iconset}/s256.png" "${iconset}/icon_256x256.png"
cp "${iconset}/s512.png" "${iconset}/icon_256x256@2x.png"
cp "${iconset}/s512.png" "${iconset}/icon_512x512.png"
cp "${iconset}/s1024.png" "${iconset}/icon_512x512@2x.png"
rm -f "${iconset}"/s*.png
iconutil -c icns "${iconset}" -o "${app}/Contents/Resources/ShaderToMP4.icns"

cat > "${app}/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>ShaderToMP4</string>
    <key>CFBundleDisplayName</key><string>ShaderToMP4</string>
    <key>CFBundleExecutable</key><string>ShaderToMP4</string>
    <key>CFBundleIdentifier</key><string>io.github.ap3demak-rgb.shadertomp4</string>
    <key>CFBundleIconFile</key><string>ShaderToMP4</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${version}</string>
    <key>CFBundleVersion</key><string>${version}</string>
    <key>LSMinimumSystemVersion</key><string>10.13</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSHumanReadableCopyright</key><string>Copyright (C) 2026 Patrick JAILLET - GPL-3.0-or-later</string>
</dict>
</plist>
PLIST

codesign --force --deep --sign - "${app}"
cp COPYING LICENSE THIRD_PARTY_NOTICES.md THIRD_PARTY_NOTICES.fr.md README.md README.fr.md "${root}/"
cp lang/*.json "${root}/lang/"
(cd dist && ditto -c -k --keepParent "${name}" "${name}.zip")
echo "dist/${name}.zip"
