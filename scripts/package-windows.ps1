# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET
# Archive portable Windows : dist\ShaderToMP4-<version>-win64.zip (un dossier : l'archive portable de reference ; l'installeur est fabrique par package-installer.ps1).
# Usage : .\scripts\package-windows.ps1 [-Pro]   (apres .\scripts\build.bat ou la compilation CMake)
# -Pro : archive de l'edition Pro (dist\ShaderToMP4-<version>-win64-pro.zip), qui livre aussi le contrat d'utilisation EULA.md / EULA.fr.md a cote du programme ;
# l'archive de l'edition libre ne les porte pas.
param([switch]$Pro)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$version = (Select-String -Path src\app_info.h -Pattern '^#define APP_VER_STR "(.*)"').Matches[0].Groups[1].Value
$name = "ShaderToMP4-$version-win64" + $(if ($Pro) { "-pro" } else { "" })
$root = "dist\$name"
if (Test-Path $root) { Remove-Item $root -Recurse -Force }
New-Item -ItemType Directory -Path "$root\lang" -Force | Out-Null
Copy-Item bin\ShaderToMP4.exe $root
Copy-Item COPYING, LICENSE, THIRD_PARTY_NOTICES.md, THIRD_PARTY_NOTICES.fr.md, README.md, README.fr.md $root
Copy-Item lang\*.json "$root\lang"
if ($Pro) { Copy-Item EULA.md, EULA.fr.md $root }
New-Item -ItemType Directory -Path "$root\transitions", "$root\examples\effects" -Force | Out-Null
Copy-Item transitions\*.glsl "$root\transitions"
Copy-Item examples\effects\*.glsl "$root\examples\effects"
New-Item -ItemType Directory -Path "$root\examples\gallery" -Force | Out-Null
Copy-Item examples\gallery\* "$root\examples\gallery" -Recurse   # galerie : shaders, passes et vignettes
$zip = "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $root -DestinationPath $zip
Write-Output $zip
