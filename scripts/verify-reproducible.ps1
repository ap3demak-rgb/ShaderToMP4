# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Construction reproductible : deux fabrications du meme commit, dans le meme dossier, doivent donner les memes octets.
.DESCRIPTION
  Fabrique le programme avec CMake (Release), note la somme SHA-256 de bin\ShaderToMP4.exe, efface le dossier de construction et recommence.
  L'edition de liens utilise /Brepro (pas d'horodatage dans l'entete de l'executable). Code de sortie 0 = les deux sommes sont identiques.
  Differences connues, hors de ce controle : un autre dossier de travail (le chemin du dossier de construction apparait dans l'entete de debogage)
  et une autre version de Visual Studio donnent d'autres octets ; les paquets (zip, installeur) portent des dates et ne sont pas comparables octet a octet.
#>
param([string]$Dir = "build-repro")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)

function Build-Once {
  if (Test-Path $Dir) { Remove-Item $Dir -Recurse -Force }
  cmake -S . -B $Dir -A x64 | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "cmake (configuration) a echoue" }
  cmake --build $Dir --config Release | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "cmake (construction) a echoue" }
  (Get-FileHash bin\ShaderToMP4.exe -Algorithm SHA256).Hash.ToLower()
}

$a = Build-Once
Write-Output "construction 1 : $a"
$b = Build-Once
Write-Output "construction 2 : $b"
if (Test-Path $Dir) { Remove-Item $Dir -Recurse -Force }
if ($a -ne $b) { Write-Output "DIFFERENT : les deux constructions ne donnent pas les memes octets"; exit 1 }
Write-Output "IDENTIQUE : bin\ShaderToMP4.exe est reproductible (SHA-256 $a)"
