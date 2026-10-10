@echo off
rem Auto-test : rend les shaders de tests\ et les compare aux references (.ref). Code de sortie 0 = OK.
rem Apres un changement voulu du rendu : scripts\test.bat --update
cd /d "%~dp0.."
if not exist bin\ShaderToMP4.exe ( echo Lance d'abord scripts\build.bat & exit /b 1 )
start "" /wait bin\ShaderToMP4.exe --selftest --quiet %*
set RC=%ERRORLEVEL%
type tests\report.txt
exit /b %RC%
