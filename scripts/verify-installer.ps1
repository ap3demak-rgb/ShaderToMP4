# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Essaie l'installeur Windows sur ce poste : installation silencieuse, fichiers et registre, ffmpeg embarque (sans reseau), reglages gardes,
  mise a jour, refus d'une version plus ancienne, refus de Program Files, programme en cours, composants facultatifs, desinstallation
  (avec et sans suppression des reglages) et traces laissees. Code de sortie 0 = tout passe.
.DESCRIPTION
  Utilise le registre et le menu Demarrer de l'utilisateur courant (ce que fait l'installeur) puis les nettoie par la desinstallation.
  Refuse de commencer si ShaderToMP4 est deja installe pour cet utilisateur. Fabrique deux installeurs d'essai (1.0.0 et 9.9.9, jamais
  publies) avec scripts\package-installer.ps1 -Version.
#>
param([string]$Setup = "", [switch]$SkipRender)   # -SkipRender : sans les essais qui lancent le programme (machine sans OpenGL 3.3, integration continue)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$appVer = (Select-String -Path src\app_info.h -Pattern '^#define APP_VER_STR "(.*)"').Matches[0].Groups[1].Value
if (-not $Setup) { $Setup = "dist\ShaderToMP4-$appVer-win64-setup.exe" }
if (-not (Test-Path $Setup)) { throw "installeur introuvable : $Setup (lance scripts\package-installer.ps1)" }
$Setup = (Resolve-Path $Setup).Path

$regApp = "HKCU:\Software\ShaderToMP4"
$regUn = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ShaderToMP4"
$regGlsl = "HKCU:\Software\Classes\.glsl\OpenWithProgids"
$regProg = "HKCU:\Software\Classes\ShaderToMP4.File"
$startLnk = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\ShaderToMP4\ShaderToMP4.lnk"
$deskLnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "ShaderToMP4.lnk"
$glslBefore = Test-Path "HKCU:\Software\Classes\.glsl"; $montageBefore = Test-Path "HKCU:\Software\Classes\.montage"   # cles d'autres programmes : l'installeur n'y touche pas
if ((Test-Path $regApp) -or (Test-Path $regUn)) { throw "ShaderToMP4 est deja installe pour cet utilisateur : desinstallez-le avant l'essai" }

$fails = New-Object System.Collections.Generic.List[string]
function Check([bool]$ok, [string]$what) { if ($ok) { Write-Host "PASS  $what" } else { Write-Host "FAIL  $what" -ForegroundColor Red; $script:fails.Add($what) } }
function Run([string]$exe, [string[]]$a) { $p = Start-Process $exe -ArgumentList $a -Wait -PassThru -NoNewWindow; $p.ExitCode }
function Hash([string]$p) { if (Test-Path $p) { (Get-FileHash $p -Algorithm SHA256).Hash.ToLower() } else { "" } }
function Install([string]$setup, [string]$dir, [string[]]$extra = @()) { Run $setup (@("/S") + $extra + @("/D=$dir")) }
# Desinstallation comme la lance Windows (Uninstall.exe /S) : le desinstalleur se copie dans TEMP, rend la main aussitot et supprime tout, y
# compris lui-meme ; on attend donc la disparition de la cle de registre et du processus copie
function Uninstall([string]$dir, [string[]]$extra = @()) {
  Start-Process "$dir\Uninstall.exe" -ArgumentList (@("/S") + $extra) | Out-Null
  for ($i = 0; $i -lt 120; $i++) {
    Start-Sleep -Milliseconds 500
    if (-not (Test-Path $regApp) -and -not (Get-Process | Where-Object { $_.Name -like "Un_*" })) { break }
  }
  Start-Sleep -Seconds 1
}
# Variante qui attend le code de sortie (le desinstalleur travaille sur place et ne peut alors pas se supprimer) : pour les refus
function UninstallInPlace([string]$dir, [string[]]$extra = @()) { Run "$dir\Uninstall.exe" (@("/S") + $extra + @("_?=$dir")) }
function Cleanup() {
  foreach ($d in $script:made) { if (Test-Path "$d\Uninstall.exe") { Uninstall $d @("/DELETE_SETTINGS") | Out-Null } }
  foreach ($d in $script:made) { if (Test-Path $d) { Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue } }
}

$root = Join-Path ([IO.Path]::GetTempPath()) ("stm_installer_" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force $root | Out-Null
$made = @()
$expected = @("ShaderToMP4.exe", "ffmpeg.exe", "COPYING", "LICENSE", "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.fr.md", "README.md", "README.fr.md", "FFMPEG-LICENSE.txt", "FFMPEG-README.txt", "Uninstall.exe")
try {
  Write-Host "Fabrication des installeurs d'essai 1.0.0 et 9.9.9..."
  & powershell -ExecutionPolicy Bypass -File scripts\package-installer.ps1 -Version 1.0.0 | Out-Null
  & powershell -ExecutionPolicy Bypass -File scripts\package-installer.ps1 -Version 9.9.9 | Out-Null
  $old = "dist\ShaderToMP4-1.0.0-win64-setup.exe"; $new = "dist\ShaderToMP4-9.9.9-win64-setup.exe"

  # --- installation silencieuse ---
  $dir = "$root\inst"; $made += $dir
  $before = @(Get-ChildItem (Split-Path $startLnk -Parent) -ErrorAction SilentlyContinue)
  $rc = Install $Setup $dir
  Check ($rc -eq 0) "installation silencieuse (code $rc)"
  $missing = @($expected | Where-Object { -not (Test-Path "$dir\$_") })
  Check ($missing.Count -eq 0) ("fichiers installes" + $(if ($missing) { " : manque " + ($missing -join ", ") } else { "" }))
  $langs = @(Get-ChildItem "$dir\lang" -Filter *.json -ErrorAction SilentlyContinue).Count
  Check ($langs -eq @(Get-ChildItem lang -Filter *.json).Count) "fichiers de langue ($langs)"
  Check ((Hash "$dir\ShaderToMP4.exe") -eq (Hash "bin\ShaderToMP4.exe")) "l'executable installe est celui de bin\ (octet pour octet)"
  $ffInfo = & "$dir\ffmpeg.exe" -hide_banner -version 2>&1 | Out-String
  Check ($ffInfo -match "ffmpeg version 9\.0\.2" -and $ffInfo -match "--enable-gpl" -and $ffInfo -notmatch "--enable-nonfree") "ffmpeg embarque : version 9.0.2, GPL, sans nonfree"
  Check ((Test-Path $regApp) -and ((Get-ItemProperty $regApp).Version -eq $appVer) -and ((Get-ItemProperty $regApp).InstallDir -eq $dir)) "registre : dossier et version"
  Check ((Test-Path $regUn) -and ((Get-ItemProperty $regUn).DisplayVersion -eq $appVer) -and ((Get-ItemProperty $regUn).UninstallString -like "*Uninstall.exe*")) "entree « Applications et fonctionnalites »"
  Check (Test-Path $startLnk) "raccourci du menu Demarrer"
  Check (-not (Test-Path $deskLnk) -and -not (Test-Path $regProg) -and -not (Test-Path $regGlsl)) "raccourci du bureau et association : absents par defaut"
  $ver = & "$dir\ShaderToMP4.exe" --version 2>&1 | Out-String
  Check ($ver -match [regex]::Escape($appVer)) "l'executable installe se lance (--version)"

  # --- ffmpeg embarque : rendu sans reseau, sans telechargement ---
  if (-not $SkipRender) {
  $work = "$root\work"; New-Item -ItemType Directory -Force $work | Out-Null
  Set-Content "$work\t.glsl" "void mainImage(out vec4 c, in vec2 f) { c = vec4(f / iResolution.xy, 0.5, 1.0); }" -Encoding ascii
  $fmts = @(@("h264", "mp4"), @("h265", "mp4"), @("vp9", "webm"), @("av1", "mkv"), @("prores_hq", "mov"), @("h264", "gif"))
  $bad = @()
  foreach ($f in $fmts) {
    $o = "$work\out_$($f[0])_$($f[1])"; New-Item -ItemType Directory -Force $o | Out-Null
    $rc = Run "$dir\ShaderToMP4.exe" @("--render", "--width=64", "--height=64", "--fps=10", "--duration=0.5", "--codec=$($f[0])", "--container=$($f[1])", "--audio=", "--outdir=$o", "$work\t.glsl")
    if ($rc -ne 0 -or -not (Get-ChildItem $o -File -ErrorAction SilentlyContinue)) { $bad += "$($f[0])/$($f[1])" }
  }
  Check ($bad.Count -eq 0) ("rendu avec l'ffmpeg installe, sans reseau" + $(if ($bad) { " : echec " + ($bad -join ", ") } else { " (" + $fmts.Count + " formats)" }))
  Check (-not (Test-Path "$dir\ffmpeg_tmp") -and -not (Test-Path "$dir\ffmpeg_download.tmp")) "aucun telechargement de ffmpeg"
  }

  # --- reglages gardes a la mise a jour ---
  Set-Content "$dir\config.ini" "fps=24`r`n" -Encoding ascii
  New-Item -ItemType Directory -Force "$dir\presets" | Out-Null; Set-Content "$dir\presets\mien.ini" "crf=18`r`n" -Encoding ascii
  # une installation d'une version plus ancienne que celle installee est refusee
  Check ((Install $old $dir) -ne 0 -and (Get-ItemProperty $regApp).Version -eq $appVer) "version plus ancienne : refusee, la version installee reste"
  $rc = Install $new $dir
  Check ($rc -eq 0 -and (Get-ItemProperty $regApp).Version -eq "9.9.9") "mise a jour vers 9.9.9 sur le meme dossier (code $rc)"
  Check ((Test-Path "$dir\config.ini") -and (Get-Content "$dir\config.ini" -Raw) -match "fps=24" -and (Test-Path "$dir\presets\mien.ini")) "reglages et prereglages intacts apres la mise a jour"
  Check ((Get-ItemProperty $regUn).DisplayVersion -eq "9.9.9") "entree de desinstallation mise a jour"

  # --- programme en cours ---
  if (-not $SkipRender) {
  $app = Start-Process "$dir\ShaderToMP4.exe" -PassThru
  Start-Sleep -Seconds 4
  $rc = Install $new $dir
  Check ($rc -ne 0) "programme en cours d'execution : l'installeur silencieux s'arrete (code $rc)"
  $rcU = UninstallInPlace $dir
  Check ($rcU -ne 0 -and (Test-Path "$dir\ShaderToMP4.exe")) "programme en cours d'execution : le desinstalleur s'arrete aussi, rien n'est supprime"
  Get-Process ShaderToMP4 -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Seconds 1
  }

  # --- desinstallation : reglages gardes ---
  Uninstall $dir
  Check (-not (Test-Path $regApp)) "desinstallation silencieuse"
  $left = @(Get-ChildItem $dir -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName.Substring($dir.Length + 1) } | Sort-Object)
  Check (($left -join "|") -eq "config.ini|presets\mien.ini") ("seuls les reglages restent : " + ($left -join ", "))
  Check (-not (Test-Path $regApp) -and -not (Test-Path $regUn) -and -not (Test-Path $startLnk)) "registre et raccourci retires"
  Remove-Item $dir -Recurse -Force

  # --- composants facultatifs, puis desinstallation avec suppression des reglages ---
  $rc = Install $Setup $dir @("/DESKTOP", "/ASSOC")
  Check ($rc -eq 0 -and (Test-Path $deskLnk) -and (Test-Path $regProg) -and ((Get-ItemProperty $regGlsl).PSObject.Properties.Name -contains "ShaderToMP4.File")) "composants facultatifs : raccourci du bureau et « Ouvrir avec »"
  $cmd = (Get-ItemProperty "$regProg\shell\open\command")."(default)"
  Check ($cmd -like "*$dir\ShaderToMP4.exe*") "« Ouvrir avec » pointe sur le programme installe"
  Set-Content "$dir\config.ini" "fps=30`r`n" -Encoding ascii
  Uninstall $dir @("/DELETE_SETTINGS")
  Check (-not (Test-Path $dir) -and -not (Test-Path $deskLnk) -and -not (Test-Path $regProg) -and ((Test-Path "HKCU:\Software\Classes\.glsl") -eq $glslBefore) -and ((Test-Path "HKCU:\Software\Classes\.montage") -eq $montageBefore) -and ((Get-ItemProperty "HKCU:\Software\Classes\.glsl\OpenWithProgids" -ErrorAction SilentlyContinue).PSObject.Properties.Name -notcontains "ShaderToMP4.File")) "desinstallation avec suppression des reglages : dossier, raccourcis et cles retires"

  # --- ffmpeg de l'utilisateur : jamais ecrase, jamais supprime ---
  $dir2 = "$root\inst2"; $made += $dir2
  New-Item -ItemType Directory -Force $dir2 | Out-Null
  Set-Content "$dir2\ffmpeg.exe" "ffmpeg de l'utilisateur" -Encoding ascii
  $mine = Hash "$dir2\ffmpeg.exe"
  $rc = Install $Setup $dir2
  Check ($rc -eq 0 -and (Hash "$dir2\ffmpeg.exe") -eq $mine -and (Get-ItemProperty $regApp).FfmpegInstalled -eq 0) "un ffmpeg.exe de l'utilisateur n'est pas ecrase"
  $rc = Install $new $dir2
  Check ((Hash "$dir2\ffmpeg.exe") -eq $mine) "... ni a la mise a jour"
  Uninstall $dir2 | Out-Null
  Check ((Hash "$dir2\ffmpeg.exe") -eq $mine -and -not (Test-Path "$dir2\ShaderToMP4.exe")) "... ni a la desinstallation (le reste est supprime)"
  Remove-Item $dir2 -Recurse -Force

  # --- Program Files refuse ---
  $pf = Join-Path $env:ProgramFiles "ShaderToMP4_essai"
  $rc = Install $Setup $pf
  Check ($rc -ne 0 -and -not (Test-Path $pf) -and -not (Test-Path $regApp)) "Program Files : refuse, rien n'est installe (code $rc)"

  # --- traces : plus rien du programme ---
  Check (-not (Test-Path $regApp) -and -not (Test-Path $regUn) -and -not (Test-Path $startLnk) -and -not (Test-Path $deskLnk) -and -not (Test-Path $regProg)) "aucune trace (registre, raccourcis) apres les desinstallations"
  $after = @(Get-ChildItem (Split-Path $startLnk -Parent) -ErrorAction SilentlyContinue)
  Check ($after.Count -eq $before.Count) "menu Demarrer comme avant"
} finally {
  Cleanup
  Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item "dist\ShaderToMP4-1.0.0-win64-setup.exe", "dist\ShaderToMP4-9.9.9-win64-setup.exe" -ErrorAction SilentlyContinue
}
if ($fails.Count -gt 0) { Write-Host "$($fails.Count) echec(s)" -ForegroundColor Red; exit 1 }
Write-Host "Installeur conforme." -ForegroundColor Green
exit 0
