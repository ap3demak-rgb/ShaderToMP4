@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Patrick JAILLET
rem Construction pour la mesure de couverture des tests : bin-cov\ShaderToMP4.exe, compile par clang-cl (LLVM) avec la couverture par source
rem (-fprofile-instr-generate -fcoverage-mapping), sans optimisation. Pour l'essai seulement, jamais livre.
rem Lancer ensuite : scripts\coverage.ps1  (auto-test complet, puis rapport par fichier dans coverage\)
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" if defined VCVARSALL ( set "VCVARS=%VCVARSALL%" )
if not exist "%VCVARS%" (
    echo vcvars64.bat introuvable : adapte le chemin VCVARS dans scripts\build-coverage.bat
    exit /b 1
)
set "LLVM=C:\Program Files\LLVM"
if not exist "%LLVM%\bin\clang-cl.exe" (
    echo clang-cl.exe introuvable dans "%LLVM%" : installer LLVM ou adapter la variable LLVM
    exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0.."
if not exist build-cov mkdir build-cov
if not exist bin-cov mkdir bin-cov
rc /nologo /I src /I res /fo build-cov\app.res res\app.rc || exit /b 1
set "PROFLIB="
for /d %%V in ("%LLVM%\lib\clang\*") do if exist "%%V\lib\windows\clang_rt.profile-x86_64.lib" set "PROFLIB=%%V\lib\windows\clang_rt.profile-x86_64.lib"
if not defined PROFLIB (
    echo clang_rt.profile-x86_64.lib introuvable dans "%LLVM%\lib\clang"
    exit /b 1
)
"%LLVM%\bin\clang-cl.exe" /nologo /Od /Zi /EHsc /std:c++17 /utf-8 /MT -fprofile-instr-generate -fcoverage-mapping -Wno-everything /I src /Fe:bin-cov\ShaderToMP4.exe /Fo:build-cov\ /Fd:bin-cov\ShaderToMP4.pdb src\main.cpp src\core\*.cpp src\platform\*.cpp src\platform\windows\*.cpp src\gui\*.cpp build-cov\app.res "%PROFLIB%" opengl32.lib gdi32.lib user32.lib comctl32.lib comdlg32.lib shell32.lib /link /MANIFEST:NO /DEBUG || exit /b 1
echo bin-cov\ShaderToMP4.exe pret
