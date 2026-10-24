# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Couverture des tests : lance l'auto-test complet sur bin-cov\ShaderToMP4.exe et ecrit le rapport par fichier dans coverage\report.txt.
.DESCRIPTION
  A lancer apres scripts\build-coverage.bat (clang-cl, couverture par source). Le binaire est copie avec ffmpeg a cote ; chaque processus lance par l'auto-test
  (fuzzing, plantage volontaire, ligne de commande) ecrit son profil dans coverage\, puis llvm-profdata et llvm-cov les fusionnent. Le rapport donne le total du moteur
  (src\core), des fenetres (src\gui, pilotees par messages et non par l'auto-test : 0 % attendu) et du reste, puis les fichiers du moteur les moins couverts.
  -Min : seuil des lignes du moteur en pour cent ; en dessous, le script rend le code 1. Objectif ecrit dans ROADMAP.md : 80 %.
#>
param([double]$Min = 80, [string]$Llvm = "C:\Program Files\LLVM\bin")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
if (-not (Test-Path bin-cov\ShaderToMP4.exe)) { throw "bin-cov\ShaderToMP4.exe introuvable : lancer scripts\build-coverage.bat" }
foreach ($t in "llvm-profdata.exe", "llvm-cov.exe") { if (-not (Test-Path "$Llvm\$t")) { throw "$t introuvable dans $Llvm" } }
if ((Test-Path bin\ffmpeg.exe) -and -not (Test-Path bin-cov\ffmpeg.exe)) { Copy-Item bin\ffmpeg.exe bin-cov\ }
Remove-Item coverage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory coverage | Out-Null
$env:LLVM_PROFILE_FILE = (Resolve-Path coverage).Path + "\st-%p.profraw"
$p = Start-Process bin-cov\ShaderToMP4.exe -ArgumentList "--selftest", "--quiet" -Wait -PassThru -NoNewWindow
if ($p.ExitCode -ne 0) { throw "l'auto-test echoue (code $($p.ExitCode)) : voir tests\report.txt" }
& "$Llvm\llvm-profdata.exe" merge -sparse (Get-ChildItem coverage\*.profraw | ForEach-Object FullName) -o coverage\all.profdata
if ($LASTEXITCODE -ne 0) { throw "llvm-profdata a echoue" }
# les arguments avec un point doivent rester entre guillemets, sinon PowerShell coupe « -instr-profile=...all.profdata »
& "$Llvm\llvm-cov.exe" report bin-cov\ShaderToMP4.exe "-instr-profile=coverage\all.profdata" "-ignore-filename-regex=(Program Files|build)" 2>$null | Out-File coverage\report.txt -Encoding utf8
$rows = Get-Content coverage\report.txt | Where-Object { $_ -match '^src' } | ForEach-Object {
  $f = $_ -split '\s+'
  [pscustomobject]@{ File = $f[0]; Lines = [int]$f[7]; Missed = [int]$f[8] }
}
$groups = [ordered]@{ "moteur (src\core)" = "src\core*"; "fenetres (src\gui)" = "src\gui*"; "reste (main, platform)" = "src\main*", "src\platform*" }
$core = 0.0
foreach ($g in $groups.GetEnumerator()) {
  $sel = $rows | Where-Object { $f = $_.File; ($g.Value | Where-Object { $f -like $_ }) }
  $n = ($sel | Measure-Object Lines -Sum).Sum; $m = ($sel | Measure-Object Missed -Sum).Sum
  $pct = if ($n) { 100.0 * (1 - $m / $n) } else { 0.0 }
  if ($g.Key -like "moteur*") { $core = $pct }
  "{0,-26} {1,6} lignes, {2,6} non couvertes : {3,5:N1} %" -f $g.Key, $n, $m, $pct
}
"Fichiers du moteur les moins couverts :"
$rows | Where-Object { $_.File -like "src\core*.cpp" -and $_.Lines -ge 20 } | Sort-Object { 1 - $_.Missed / $_.Lines } | Select-Object -First 12 |
  ForEach-Object { "  {0,-36} {1,5} lignes, {2,5:N1} %" -f $_.File, $_.Lines, (100.0 * (1 - $_.Missed / $_.Lines)) }
if ($core -lt $Min) { Write-Output ("Couverture du moteur sous le seuil de {0} %." -f $Min); exit 1 }
Write-Output "Rapport complet : coverage\report.txt"
