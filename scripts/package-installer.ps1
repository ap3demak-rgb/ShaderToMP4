# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Fabrique l'installeur Windows : dist\ShaderToMP4-<version>-win64-setup.exe (NSIS 3), a cote de l'archive portable.
.DESCRIPTION
  Prerequis : bin\ShaderToMP4.exe (scripts\build.bat ou CMake) et makensis (NSIS 3).
  1. La version est celle de src\app_info.h ; avec un tag « v* » (GITHUB_REF_NAME) elle doit lui etre egale.
  2. ffmpeg.exe est telecharge en version epinglee (meme source que --get-ffmpeg : gyan.dev, construction « essentials »), sa somme SHA-256
     est controlee, et la construction doit etre GPL sans --enable-nonfree (seule redistribuable avec ce programme).
  3. Les fichiers a installer sont rassembles dans build\installer\stage, les fichiers generes (textes des neuf langues tires de lang\*.json,
     listes de fichiers, licence) dans build\installer\gen, puis makensis construit l'installeur.
  -Version permet de fabriquer un installeur d'un autre numero pour les essais de mise a jour (jamais publie).
#>
param([string]$Version = "")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

# --- Epingle : version et somme de ffmpeg embarque (a changer ensemble avec THIRD_PARTY_NOTICES) ---
$ffVersion = "9.0.2"
$ffUrl = "https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-$ffVersion-essentials_build.zip"
$ffSha256 = "60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba"

function Meta([string]$name) { (Select-String -Path src\app_info.h -Pattern ('^#define ' + $name + ' "(.*)"')).Matches[0].Groups[1].Value }
$appVer = Meta "APP_VER_STR"
if ($env:GITHUB_REF_NAME -match '^v(.+)$' -and $Matches[1] -ne $appVer) { throw "Le tag $($env:GITHUB_REF_NAME) ne correspond pas a APP_VER_STR $appVer" }
$version = if ($Version) { $Version } else { $appVer }
$name = Meta "APP_NAME_STR"; $publisher = Meta "APP_COPYRIGHT_OWNER"; $year = Meta "APP_COPYRIGHT_YEAR"; $url = Meta "APP_WEBSITE_URL"
if (-not (Test-Path bin\ShaderToMP4.exe)) { throw "bin\ShaderToMP4.exe introuvable : lance scripts\build.bat" }

# --- makensis ---
$mk = (Get-Command makensis -ErrorAction SilentlyContinue).Source
if (-not $mk) { foreach ($d in "${env:ProgramFiles(x86)}\NSIS", "$env:ProgramFiles\NSIS") { if (Test-Path "$d\makensis.exe") { $mk = "$d\makensis.exe"; break } } }
if (-not $mk) { throw "makensis introuvable : installe NSIS 3 (https://nsis.sourceforge.io)" }

# --- ffmpeg epingle ---
$work = "build\installer"
New-Item -ItemType Directory -Force $work | Out-Null
$zip = "$work\ffmpeg-$ffVersion-essentials_build.zip"
if (-not (Test-Path $zip) -or (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower() -ne $ffSha256) {
  Write-Host "Telechargement de ffmpeg $ffVersion..."
  Invoke-WebRequest $ffUrl -OutFile $zip -UseBasicParsing
}
$got = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
if ($got -ne $ffSha256) { throw "Somme SHA-256 de ffmpeg inattendue : $got (attendue $ffSha256)" }
$ffDir = "$work\ffmpeg-$ffVersion"
if (-not (Test-Path "$ffDir\bin\ffmpeg.exe")) {
  if (Test-Path $ffDir) { Remove-Item $ffDir -Recurse -Force }
  Expand-Archive $zip "$work\ffmpeg-extract" -Force
  Move-Item (Get-ChildItem "$work\ffmpeg-extract" -Directory | Select-Object -First 1).FullName $ffDir
  Remove-Item "$work\ffmpeg-extract" -Recurse -Force
}
$ffInfo = & "$ffDir\bin\ffmpeg.exe" -hide_banner -version 2>&1 | Out-String
if ($ffInfo -notmatch "ffmpeg version $([regex]::Escape($ffVersion))") { throw "ffmpeg.exe n'est pas la version $ffVersion" }
if ($ffInfo -notmatch "--enable-gpl") { throw "ffmpeg.exe n'est pas une construction GPL : non redistribuable avec ce programme" }
if ($ffInfo -match "--enable-nonfree") { throw "ffmpeg.exe est une construction nonfree : redistribution interdite" }

# --- Fichiers a installer ---
$stage = "$work\stage"; $gen = "$work\gen"
foreach ($d in $stage, $gen) { if (Test-Path $d) { Remove-Item $d -Recurse -Force }; New-Item -ItemType Directory -Force $d | Out-Null }
New-Item -ItemType Directory -Force "$stage\lang" | Out-Null
Copy-Item bin\ShaderToMP4.exe $stage
Copy-Item "$ffDir\bin\ffmpeg.exe" $stage
Copy-Item COPYING, LICENSE, THIRD_PARTY_NOTICES.md, THIRD_PARTY_NOTICES.fr.md, README.md, README.fr.md $stage
Copy-Item "$ffDir\LICENSE" "$stage\FFMPEG-LICENSE.txt"
Copy-Item "$ffDir\README.txt" "$stage\FFMPEG-README.txt"
Copy-Item lang\*.json "$stage\lang"

# --- Fichiers generes ---
$utf8bom = New-Object System.Text.UTF8Encoding($true)
$inst = New-Object System.Text.StringBuilder; $uninst = New-Object System.Text.StringBuilder
$stageFull = (Resolve-Path $stage).Path
$lastDir = $null
foreach ($f in (Get-ChildItem $stage -Recurse -File | Sort-Object FullName)) {
  $rel = $f.FullName.Substring($stageFull.Length + 1)
  if ($rel -eq "ffmpeg.exe") { continue }   # traite a part par l'installeur (jamais ecrase s'il n'est pas le notre)
  $sub = Split-Path $rel -Parent
  if ($sub -ne $lastDir) { [void]$inst.AppendLine('  SetOutPath "$INSTDIR' + $(if ($sub) { "\$sub" } else { "" }) + '"'); $lastDir = $sub }
  [void]$inst.AppendLine('  File "' + $f.FullName + '"')
  [void]$uninst.AppendLine('  Delete "$INSTDIR\' + $rel + '"')
}
[IO.File]::WriteAllText("$gen\files_install.nsh", $inst.ToString(), $utf8bom)
[IO.File]::WriteAllText("$gen\files_uninstall.nsh", $uninst.ToString(), $utf8bom)
# licence affichee par l'installeur : UTF-16 (l'installeur est Unicode)
[IO.File]::WriteAllText("$gen\license.txt", (Get-Content COPYING -Raw -Encoding UTF8), [Text.Encoding]::Unicode)

# textes de l'application, un LangString par cle et par langue (cles installer.* de lang\*.json) ; %ls = les variables de l'installeur
$keys = "installer.sec_program", "installer.sec_startmenu", "installer.sec_desktop", "installer.sec_assoc",
        "installer.desc_program", "installer.desc_startmenu", "installer.desc_desktop", "installer.desc_assoc",
        "installer.running", "installer.older", "installer.progfiles", "installer.purge_title", "installer.purge_sub",
        "installer.purge_check", "installer.finish_run"
$holders = @{ "installer.older" = @('$InstalledVer', '${APP_VERSION}') }
$langs = [ordered]@{ en = "ENGLISH"; fr = "FRENCH"; de = "GERMAN"; es = "SPANISH"; it = "ITALIAN"; ja = "JAPANESE"; nl = "DUTCH"; pt = "PORTUGUESE"; zh = "SIMPCHINESE" }
$strings = New-Object System.Text.StringBuilder
foreach ($code in $langs.Keys) {
  $json = Get-Content "lang\$code.json" -Raw -Encoding UTF8 | ConvertFrom-Json
  foreach ($k in $keys) {
    $text = $json.PSObject.Properties[$k].Value
    if (-not $text) { throw "cle $k absente de lang\$code.json" }
    $text = $text.Replace('$', '$$').Replace('"', '$\"').Replace("`r", "").Replace("`n", '$\n')
    $i = 0
    while ($text.Contains("%ls")) {
      $h = if ($holders.ContainsKey($k) -and $i -lt $holders[$k].Count) { $holders[$k][$i] } else { throw "marque %ls inattendue dans $k ($code)" }
      $idx = $text.IndexOf("%ls"); $text = $text.Substring(0, $idx) + $h + $text.Substring($idx + 3); $i++
    }
    [void]$strings.AppendLine('LangString ' + $k.Replace('.', '_') + ' ${LANG_' + $langs[$code] + '} "' + $text + '"')
  }
}
[IO.File]::WriteAllText("$gen\strings.nsh", $strings.ToString(), $utf8bom)

# --- Fabrication ---
$sizeKb = [int](((Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum) / 1024)
$ver4 = ($version -replace '[^0-9.].*$', '')
while (($ver4.Split('.')).Count -lt 4) { $ver4 += ".0" }
New-Item -ItemType Directory -Force dist | Out-Null
$out = (Join-Path (Resolve-Path dist).Path "$name-$version-win64-setup.exe")
if (Test-Path $out) { Remove-Item $out -Force }
$full = { param($p) (Resolve-Path $p).Path }
$args = @("/V2", "/DAPP_NAME=$name", "/DAPP_VERSION=$version", "/DAPP_VER4=$ver4", "/DAPP_PUBLISHER=$publisher", "/DAPP_COPYRIGHT_YEAR=$year", "/DAPP_URL=$url",
          "/DOUTFILE=$out", "/DSTAGE=$stageFull", "/DGEN=$(& $full $gen)", "/DICON=$(& $full res\app.ico)", "/DLICENSEFILE=$(& $full $gen)\license.txt", "/DSIZE_KB=$sizeKb",
          "installer\ShaderToMP4.nsi")
& $mk @args
if ($LASTEXITCODE -ne 0) { throw "makensis a echoue ($LASTEXITCODE)" }
$hash = (Get-FileHash $out -Algorithm SHA256).Hash.ToLower()
Write-Host ("{0}  {1:N1} Mo  SHA-256 {2}" -f $out, ((Get-Item $out).Length / 1MB), $hash)
Write-Output $out
