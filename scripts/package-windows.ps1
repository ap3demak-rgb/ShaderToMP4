# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET
# Archive portable Windows : dist\ShaderToMP4-<version>-win64.zip (un dossier, aucun installeur).
# Usage : .\scripts\package-windows.ps1   (apres .\scripts\build.bat ou la compilation CMake)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$version = (Select-String -Path src\app_info.h -Pattern '^#define APP_VER_STR "(.*)"').Matches[0].Groups[1].Value
$name = "ShaderToMP4-$version-win64"
$root = "dist\$name"
if (Test-Path $root) { Remove-Item $root -Recurse -Force }
New-Item -ItemType Directory -Path "$root\lang" -Force | Out-Null
Copy-Item bin\ShaderToMP4.exe $root
Copy-Item COPYING, LICENSE, THIRD_PARTY_NOTICES.md, THIRD_PARTY_NOTICES.fr.md, README.md, README.fr.md $root
Copy-Item lang\*.json "$root\lang"
$zip = "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $root -DestinationPath $zip
Write-Output $zip
