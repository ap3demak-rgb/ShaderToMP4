@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Patrick JAILLET
rem Construction pour la mesure de couverture des tests (scripts\coverage.ps1) : bin-cov\ShaderToMP4.exe, sans optimisation et avec symboles (PDB).
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" if defined VCVARSALL ( set "VCVARS=%VCVARSALL%" )
if not exist "%VCVARS%" (
    echo vcvars64.bat introuvable : adapte le chemin VCVARS dans scripts\build-coverage.bat
    exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0.."
if not exist build-cov mkdir build-cov
if not exist bin-cov mkdir bin-cov
rc /nologo /I src /I res /fo build-cov\app.res res\app.rc || exit /b 1
cl /nologo /Od /Zi /Gy /hotpatch /EHsc /std:c++17 /utf-8 /MP /MT /I src /Fe:bin-cov\ShaderToMP4.exe /Fo:build-cov\ /Fd:bin-cov\ShaderToMP4.pdb src\main.cpp src\core\*.cpp src\platform\*.cpp src\platform\windows\*.cpp src\gui\*.cpp build-cov\app.res opengl32.lib gdi32.lib user32.lib comctl32.lib comdlg32.lib shell32.lib /link /MANIFEST:NO /DEBUG /PROFILE /FUNCTIONPADMIN || exit /b 1
echo bin-cov\ShaderToMP4.exe pret
