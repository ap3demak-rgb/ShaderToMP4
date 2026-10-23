# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Manifestes winget et Scoop de la version, fabriques a partir des fichiers de la version (installeur NSIS et archive portable).
.DESCRIPTION
  Ecrit dans dist\manifests : le manifeste winget (trois fichiers YAML, schema 1.6.0, installeur NSIS silencieux /S, installation par utilisateur) et le
  manifeste Scoop (archive portable, reglages conserves par « persist »). Les adresses sont celles des fichiers de la version GitHub
  (https://github.com/ap3demak-rgb/ShaderToMP4/releases/download/v<version>/...), les sommes SHA-256 celles des fichiers locaux : lance par la CI apres
  package-windows.ps1 et package-installer.ps1, avant la publication. Il n'y a pas de paquet pour Linux ni macOS (gelés).
  Pour proposer le paquet winget, copier le dossier winget\ dans microsoft/winget-pkgs (manifests\a\ap3demak-rgb\ShaderToMP4\<version>) ; pour Scoop, copier
  ShaderToMP4.json dans un « bucket ». Ni l'un ni l'autre n'est fait par ce script.
#>
param(
  [string]$Version = "",
  [string]$SetupPath = "",
  [string]$ZipPath = "",
  [string]$BaseUrl = "",
  [string]$OutDir = "dist\manifests"
)
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
if (-not $Version) { $Version = (Select-String -Path src\app_info.h -Pattern '^#define APP_VER_STR "(.*)"').Matches[0].Groups[1].Value }
if (-not $BaseUrl) { $BaseUrl = "https://github.com/ap3demak-rgb/ShaderToMP4/releases/download/v$Version" }
if (-not $SetupPath) { $SetupPath = "dist\ShaderToMP4-$Version-win64-setup.exe" }
if (-not $ZipPath) { $ZipPath = "dist\ShaderToMP4-$Version-win64.zip" }
foreach ($f in $SetupPath, $ZipPath) { if (-not (Test-Path $f)) { throw "fichier introuvable : $f (lancer d'abord package-windows.ps1 et package-installer.ps1)" } }
$setupName = Split-Path $SetupPath -Leaf
$zipName = Split-Path $ZipPath -Leaf
$setupSha = (Get-FileHash $SetupPath -Algorithm SHA256).Hash.ToUpper()
$zipSha = (Get-FileHash $ZipPath -Algorithm SHA256).Hash.ToLower()
$id = "ap3demak-rgb.ShaderToMP4"
$wg = Join-Path $OutDir "winget"
if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
New-Item -ItemType Directory -Force $wg | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Put($path, $text) { [IO.File]::WriteAllText($path, ($text -replace "`r`n", "`n"), $utf8) }
$head = "# yaml-language-server: `$schema=https://aka.ms/winget-manifest.{0}.1.6.0.schema.json`n`n"

Put "$wg\$id.yaml" (($head -f "version") + @"
PackageIdentifier: $id
PackageVersion: $Version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.6.0
"@)

Put "$wg\$id.installer.yaml" (($head -f "installer") + @"
PackageIdentifier: $id
PackageVersion: $Version
InstallerType: nullsoft
Scope: user
InstallModes:
- interactive
- silent
InstallerSwitches:
  Silent: /S
  SilentWithProgress: /S
UpgradeBehavior: install
Installers:
- Architecture: x64
  InstallerUrl: $BaseUrl/$setupName
  InstallerSha256: $setupSha
ManifestType: installer
ManifestVersion: 1.6.0
"@)

Put "$wg\$id.locale.en-US.yaml" (($head -f "defaultLocale") + @"
PackageIdentifier: $id
PackageVersion: $Version
PackageLocale: en-US
Publisher: Patrick JAILLET
PublisherUrl: https://ap3demak-rgb.github.io/ShaderToMP4
PublisherSupportUrl: https://github.com/ap3demak-rgb/ShaderToMP4/issues
Author: Patrick JAILLET
PackageName: ShaderToMP4
PackageUrl: https://ap3demak-rgb.github.io/ShaderToMP4
License: GPL-3.0-or-later
LicenseUrl: https://github.com/ap3demak-rgb/ShaderToMP4/blob/v$Version/COPYING
Copyright: Copyright (C) 2026 Patrick JAILLET
ShortDescription: Render Shadertoy shaders frame by frame to video with ffmpeg.
Description: ShaderToMP4 turns Shadertoy Image shaders into MP4, MOV, MKV, WebM, GIF or image sequences. It renders on the GPU with OpenGL, supports buffers, channels, supersampling, motion blur, HDR and hardware encoders, and runs from its own folder without writing anywhere else.
Moniker: shadertomp4
Tags:
- shader
- shadertoy
- glsl
- video
- ffmpeg
- render
ReleaseNotesUrl: https://github.com/ap3demak-rgb/ShaderToMP4/releases/tag/v$Version
ManifestType: defaultLocale
ManifestVersion: 1.6.0
"@)

$scoop = [ordered]@{
  version = $Version
  description = "Render Shadertoy shaders frame by frame to video with ffmpeg."
  homepage = "https://ap3demak-rgb.github.io/ShaderToMP4"
  license = "GPL-3.0-or-later"
  architecture = [ordered]@{ "64bit" = [ordered]@{ url = "$BaseUrl/$zipName"; hash = $zipSha } }
  extract_dir = "ShaderToMP4-$Version-win64"
  shortcuts = @(, @("ShaderToMP4.exe", "ShaderToMP4"))
  persist = @("config.ini", "window.ini", "queue.txt", "presets", "params", "jobs", "cache", "updates")
  checkver = [ordered]@{ github = "https://github.com/ap3demak-rgb/ShaderToMP4" }
  autoupdate = [ordered]@{ architecture = [ordered]@{ "64bit" = [ordered]@{ url = "https://github.com/ap3demak-rgb/ShaderToMP4/releases/download/v`$version/ShaderToMP4-`$version-win64.zip" } }; extract_dir = "ShaderToMP4-`$version-win64" }
}
Put "$OutDir\ShaderToMP4.json" ($scoop | ConvertTo-Json -Depth 8)
Write-Output "winget : $wg"
Write-Output "scoop  : $OutDir\ShaderToMP4.json"
