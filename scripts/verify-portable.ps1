# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Verifie que le programme est portable : pendant une vraie session (rendu d'un shader, aperçu, changement de langue),
  il ne cree aucun fichier ni cle de registre en dehors de son propre dossier.
.DESCRIPTION
  - Le programme est copie dans un dossier temporaire neuf (exe + ffmpeg.exe) et lance avec un profil utilisateur
    (USERPROFILE, APPDATA, LOCALAPPDATA, TEMP) redirige vers un dossier vide : tout ce qu'il y ecrirait y serait visible.
  - Les caches des pilotes graphiques (NVIDIA, AMD, Intel, D3DSCache) sont ecrits par le pilote, pas par le programme :
    ils sont listes mais ignores.
  - Le registre est inspecte : aucune cle contenant le nom du programme, aucune preference GPU enregistree pour son chemin.
  Code de sortie 0 = portable.
#>
param([string]$Exe = "bin\ShaderToMP4.exe", [string]$Ffmpeg = "bin\ffmpeg.exe")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
if (-not (Test-Path $Exe)) { Write-Host "Executable introuvable : $Exe (lance scripts\build.bat)"; exit 2 }
if (-not (Test-Path $Ffmpeg)) { Write-Host "ffmpeg.exe introuvable : $Ffmpeg"; exit 2 }

function RegQuery([string]$a) { (cmd /c "reg query $a 2>nul") -join "`n" }

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("stm_portable_" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$app = Join-Path $root "app"; $profileDir = Join-Path $root "profile"; $work = Join-Path $root "work"
New-Item -ItemType Directory -Force $app, $profileDir, "$profileDir\AppData\Roaming", "$profileDir\AppData\Local", "$profileDir\Temp", $work | Out-Null
Copy-Item $Exe "$app\ShaderToMP4.exe"; Copy-Item $Ffmpeg "$app\ffmpeg.exe"
@"
// duration=1
// fps=30
// width=320
// height=180
void mainImage(out vec4 c, in vec2 f) { c = vec4(f / iResolution.xy, 0.5, 1.0); }
"@ | Set-Content -Encoding utf8 "$work\t.glsl"

# Etat du registre avant
$before = RegQuery "HKCU\Software\Microsoft\DirectX\UserGpuPreferences"

# Session : rendu d'un shader, puis aperçu, lecture, changement de langue, A propos, fermeture (identifiants lus dans src\gui\app.h)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class PortableUi {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
$env:USERPROFILE = $profileDir; $env:APPDATA = "$profileDir\AppData\Roaming"; $env:LOCALAPPDATA = "$profileDir\AppData\Local"
$env:TEMP = "$profileDir\Temp"; $env:TMP = "$profileDir\Temp"; $env:HOMEPATH = $profileDir
$p = Start-Process "$app\ShaderToMP4.exe" -ArgumentList "`"$work\t.glsl`"" -PassThru -WorkingDirectory $work
Start-Sleep 7; $p.Refresh()
$enumText = [System.IO.File]::ReadAllText("src\gui\app.h")
$enumBody = [regex]::Match($enumText, 'enum\s*\{(.*?)\};', 'Singleline').Groups[1].Value -replace '//[^\r\n]*', ''
$names = @($enumBody -split ',' | ForEach-Object { ($_ -split '=')[0].Trim() } | Where-Object { $_ })
$base = 120
function CtrlId([string]$n) { $base + [array]::IndexOf($names, $n) }
foreach ($id in (CtrlId "ID_PREVBTN"), (CtrlId "ID_PLAY"), (CtrlId "ID_PLAY"), (CtrlId "ID_LANG")) { [void][PortableUi]::PostMessage($p.MainWindowHandle, 0x111, [IntPtr]$id, [IntPtr]::Zero); Start-Sleep -Seconds 2 }
[void]$p.CloseMainWindow(); Start-Sleep 2
if (-not $p.HasExited) { Stop-Process $p.Id -Force }

$problems = New-Object System.Collections.Generic.List[string]
# 1. Profil utilisateur : rien d'ecrit par le programme (hors caches de pilotes)
$driverCache = "NVIDIA|AMD|Intel|D3DSCache|DXCache|GLCache|Microsoft\\Windows\\INetCache|CrashDumps"
$written = Get-ChildItem $profileDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notmatch $driverCache }
foreach ($f in $written) { $problems.Add("fichier ecrit hors du dossier de l'application : $($f.FullName.Substring($profileDir.Length))") }
$ignored = @(Get-ChildItem $profileDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match $driverCache }).Count
# 2. Dossier de l'application : seuls ses fichiers attendus
$expected = "ShaderToMP4.exe", "ffmpeg.exe", "config.ini", "window.ini", "queue.txt"
Get-ChildItem $app -Recurse -File | ForEach-Object { if ($expected -notcontains $_.Name) { $problems.Add("fichier inattendu dans le dossier de l'application : $($_.Name)") } }
# 3. Sortie a cote du shader (comportement voulu) : la video doit exister
if (-not (Test-Path "$work\t.mp4")) { $problems.Add("la video de test n'a pas ete creee (la session n'a pas abouti)") }
# 4. Registre
$after = RegQuery "HKCU\Software\Microsoft\DirectX\UserGpuPreferences"
if ($after -ne $before) { $problems.Add("la preference GPU de Windows a ete modifiee") }
$found = (RegQuery "HKCU /f ShaderToMP4 /s")
if ($found -match "End of search: [1-9]|Fin de la recherche : [1-9]") { $problems.Add("cle de registre contenant le nom du programme") }

Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Fichiers de pilotes graphiques ignores : $ignored"
if ($problems.Count -gt 0) {
  Write-Host "$($problems.Count) probleme(s) de portabilite :" -ForegroundColor Red
  $problems | ForEach-Object { Write-Host "  - $_" }
  exit 1
}
Write-Host "Programme portable : aucune ecriture hors de son dossier." -ForegroundColor Green
exit 0
