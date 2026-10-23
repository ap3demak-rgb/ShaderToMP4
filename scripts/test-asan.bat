@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Patrick JAILLET
rem Auto-test complet avec la construction AddressSanitizer (scripts\build-asan.bat) : code de sortie 0 et aucune alerte sur la sortie d'erreur.
cd /d "%~dp0.."
if not exist bin-asan\ShaderToMP4.exe ( echo lancer d'abord scripts\build-asan.bat & exit /b 1 )
if not exist bin-asan\ffmpeg.exe if exist bin\ffmpeg.exe copy /y bin\ffmpeg.exe bin-asan\ >nul
powershell -NoProfile -Command "$p = Start-Process bin-asan\ShaderToMP4.exe -ArgumentList '--selftest','--quiet' -Wait -PassThru -NoNewWindow -RedirectStandardError build-asan\asan-stderr.txt; $e = Get-Content build-asan\asan-stderr.txt -ErrorAction SilentlyContinue; if ($e) { $e | Select-Object -First 40 }; Get-Content tests\report.txt | Select-String '^FAIL'; if ($p.ExitCode -ne 0 -or $e) { exit 1 }"
