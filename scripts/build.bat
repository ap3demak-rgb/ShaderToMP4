@echo off
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
rc /nologo /I src /fo build\app.res res\app.rc || exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /MP /I src /Fe:bin\ShaderToMP4.exe /Fo:build\ src\main.cpp src\core\*.cpp src\gui\*.cpp build\app.res opengl32.lib gdi32.lib user32.lib comctl32.lib comdlg32.lib shell32.lib /link /MANIFEST:EMBED || exit /b 1
del bin\ShaderToMP4.lib bin\ShaderToMP4.exp 2>nul
