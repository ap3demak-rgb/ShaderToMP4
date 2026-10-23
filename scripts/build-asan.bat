@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Patrick JAILLET
rem Construction avec le detecteur d'erreurs de memoire de Windows (AddressSanitizer de Visual Studio, /fsanitize=address) : bin-asan\ShaderToMP4.exe.
rem Pour l'essai seulement (jamais livre) : sans optimisation, avec symboles, runtime dynamique (/MD) et la DLL du detecteur copiee a cote.
rem Lancer ensuite : scripts\test-asan.bat   (auto-test complet, sans alerte attendue)
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" if defined VCVARSALL ( set "VCVARS=%VCVARSALL%" )
if not exist "%VCVARS%" (
    echo vcvars64.bat introuvable : adapte le chemin VCVARS dans scripts\build-asan.bat
    exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0.."
if not exist build-asan mkdir build-asan
if not exist bin-asan mkdir bin-asan
rc /nologo /I src /I res /fo build-asan\app.res res\app.rc || exit /b 1
cl /nologo /Od /Zi /EHsc /std:c++17 /utf-8 /MP /MD /fsanitize=address /I src /Fe:bin-asan\ShaderToMP4.exe /Fo:build-asan\ /Fd:build-asan\vc.pdb src\main.cpp src\core\*.cpp src\platform\*.cpp src\platform\windows\*.cpp src\gui\*.cpp build-asan\app.res opengl32.lib gdi32.lib user32.lib comctl32.lib comdlg32.lib shell32.lib /link /MANIFEST:NO /DEBUG /STACK:16777216 || exit /b 1
rem DLL du detecteur (dans le dossier des outils x64 du compilateur, que vcvars64 met dans le PATH)
for %%D in (clang_rt.asan_dynamic-x86_64.dll) do (
    for /f "delims=" %%P in ('where %%D 2^>nul') do copy /y "%%P" bin-asan\ >nul
)
if not exist bin-asan\clang_rt.asan_dynamic-x86_64.dll (
    echo DLL du detecteur introuvable : composant « AddressSanitizer » de Visual Studio non installe
    exit /b 1
)
echo bin-asan\ShaderToMP4.exe pret
