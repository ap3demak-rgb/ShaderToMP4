@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Patrick JAILLET
rem Compile bin\ShaderToMP4.exe avec les Build Tools de Visual Studio (a lancer depuis VS Code : .\scripts\build.bat)
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo vcvars64.bat introuvable : adapte le chemin VCVARS dans scripts\build.bat
    exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0.."
if not exist build mkdir build
if not exist bin mkdir bin
rem Le manifeste (controles modernes, DPI) est dans res\app.manifest, integre par app.rc
rc /nologo /I src /I res /fo build\app.res res\app.rc || exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /utf-8 /MP /MT /I src /Fe:bin\ShaderToMP4.exe /Fo:build\ src\main.cpp src\core\*.cpp src\platform\*.cpp src\platform\windows\*.cpp src\gui\*.cpp build\app.res opengl32.lib gdi32.lib user32.lib comctl32.lib comdlg32.lib shell32.lib /link /MANIFEST:NO || exit /b 1
del bin\ShaderToMP4.lib bin\ShaderToMP4.exp 2>nul
rem Fichiers livres avec l'application (fenetre A propos : licence et notices) et langues modifiables
copy /y COPYING bin\ >nul
copy /y LICENSE bin\ >nul
copy /y THIRD_PARTY_NOTICES.md bin\ >nul
copy /y THIRD_PARTY_NOTICES.fr.md bin\ >nul
copy /y EULA.md bin\ >nul
copy /y EULA.fr.md bin\ >nul
if not exist bin\lang mkdir bin\lang
copy /y lang\*.json bin\lang\ >nul
