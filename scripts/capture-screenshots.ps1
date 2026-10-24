# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Fabrique les captures d'ecran des README et du site (docs\screenshots\<langue>\*.png) a partir du programme lui-meme.
.DESCRIPTION
  Copie bin\ShaderToMP4.exe, ffmpeg et quatre exemples dans un dossier de travail (bin-shots\, ignore par Git) monte comme lecteur virtuel S: (subst) : les captures ne
  montrent ainsi aucun chemin personnel, et config.ini, window.ini et queue.txt du dossier bin\ ne sont pas touches. Le script ecrit des reglages et une file de quatre
  exemples, lance le programme dans la langue demandee, ouvre chaque fenetre par un message de commande et photographie la fenetre (rectangle a l'ecran, sans l'ombre).
  La fenetre principale est placee sur l'ecran principal ; ne touchez ni a la souris ni au clavier pendant l'execution (une minute et demie par langue).
  Usage : .\scripts\capture-screenshots.ps1 [-Lang en|fr|all] [-OutDir docs\screenshots]
#>
param([string]$Lang = "all", [string]$OutDir = "docs\screenshots")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Cap {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT r, int size);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
# identifiants des commandes plus recentes : relus dans l'enumeration de src\gui\app.h
$hdr = Get-Content "$root\src\gui\app.h" -Raw
$enum = [regex]::Match($hdr, 'enum \{\s*ID_LIST = 101(.*?)\n\};', 'Singleline').Value -replace '//[^\n]*', ''
$ID = @{}; $next = 101
foreach ($tok in ($enum -replace '^enum \{', '' -replace '\};$', '') -split ',') {
  $t = $tok.Trim(); if (-not $t) { continue }
  if ($t -match '^(ID_[A-Z0-9_]+)\s*=\s*(\d+)$') { $next = [int]$Matches[2]; $ID[$Matches[1]] = $next } elseif ($t -match '^(ID_[A-Z0-9_]+)$') { $ID[$Matches[1]] = $next } else { continue }
  $next++
}
$OutDir = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
$drive = "S:"
if (Test-Path "$drive\") { throw "le lecteur $drive existe deja : liberez-le (subst $drive /D) ou changez la variable `$drive" }
$work = Join-Path $root "bin-shots"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force "$work\examples", "$work\videos" | Out-Null
Copy-Item "$root\bin\ShaderToMP4.exe" $work
if (Test-Path "$root\bin\ffmpeg.exe") { Copy-Item "$root\bin\ffmpeg.exe" $work }
foreach ($n in "plasma", "aurore", "tunnel", "anneaux") { Copy-Item "$root\examples\$n.glsl" "$work\examples\" }
Copy-Item "$root\examples\gallery" "$work\examples\gallery" -Recurse   # la galerie d'exemples du programme
subst $drive $work
$exe = "$drive\ShaderToMP4.exe"
$tmpOut = "$drive\videos"
$examples = @("plasma", "aurore", "tunnel", "anneaux") | ForEach-Object { "$drive\examples\$_.glsl" }

function Write-Config($duration) {
  $c = "width=1280`nheight=720`nfps=30`nduration=$duration`ncodec=h264`ncontainer=mp4`ncrf=18`npreset=veryfast`nencoder=cpu`naudio=`noutdir=$tmpOut`nexists=overwrite`nchunk=0`n"
  [IO.File]::WriteAllText("$drive\config.ini", $c)
}
function Write-Ini($lang, $tour) { [IO.File]::WriteAllText("$drive\window.ini", "x=60 y=30 w=1016 h=939 max=0`nlang=$lang`ntour=$tour`n") }
function Start-App { $p = Start-Process $exe -PassThru; Start-Sleep -Seconds 3; $p }
function Main-Wnd { [Cap]::FindWindow("ShaderToMP4Main", [NullString]::Value) }
function Cmd($id) { [void][Cap]::PostMessage((Main-Wnd), 0x111, [IntPtr]$id, [IntPtr]::Zero) }
function Grab($hwnd, $path) {
  $r = New-Object Cap+RECT
  [void][Cap]::DwmGetWindowAttribute($hwnd, 9, [ref]$r, 16)  # DWMWA_EXTENDED_FRAME_BOUNDS : sans l'ombre
  $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
  New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
function Shot($hwnd, $path) {
  [void][Cap]::ShowWindow($hwnd, 9)
  [void][Cap]::SetForegroundWindow($hwnd)
  [void][Cap]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0003)  # HWND_TOPMOST, sans deplacer ni redimensionner
  Start-Sleep -Milliseconds 700
  Grab $hwnd $path
  [void][Cap]::SetWindowPos($hwnd, [IntPtr](-2), 0, 0, 0, 0, 0x0003)  # HWND_NOTOPMOST
}
function Crop($src, $dst, $x, $y, $w, $h) {
  $b = [System.Drawing.Bitmap]::FromFile($src)
  $w = [math]::Min($w, $b.Width - $x); $h = [math]::Min($h, $b.Height - $y)
  $c = $b.Clone((New-Object System.Drawing.Rectangle $x, $y, $w, $h), $b.PixelFormat)
  $b.Dispose(); $c.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png); $c.Dispose()
}
function Close-Wnd($class) { $h = [Cap]::FindWindow($class, [NullString]::Value); if ($h -ne [IntPtr]::Zero) { [void][Cap]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero); Start-Sleep -Milliseconds 600 } }
function Stop-App($p) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }; Start-Sleep -Milliseconds 500 }
function Clear-Videos { Get-ChildItem $tmpOut -ErrorAction SilentlyContinue | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }

try {
  $langs = if ($Lang -eq "all") { @("en", "fr") } else { @($Lang) }
  foreach ($l in $langs) {
    $out = Join-Path $OutDir $l

    # 1. fenetre vide : la zone de depot
    Write-Config 3; Write-Ini $l 0
    Remove-Item "$drive\queue.txt" -ErrorAction SilentlyContinue
    $p = Start-App
    Shot (Main-Wnd) "$out\empty.png"
    Stop-App $p

    # 2. quatre exemples dans la liste, apercu et reglages
    [IO.File]::WriteAllText("$drive\queue.txt", (($examples -join "`n") + "`n"))
    $p = Start-App
    Cmd 148; Start-Sleep -Seconds 3      # Apercu
    Shot (Main-Wnd) "$out\list-preview.png"
    Crop "$out\list-preview.png" "$out\settings.png" 0 70 2000 470
    Stop-App $p

    # 3. rendu en cours : des videos plus longues, photographiees en route puis annulees
    Write-Config 40; Clear-Videos
    [IO.File]::WriteAllText("$drive\queue.txt", (($examples -join "`n") + "`n"))
    $p = Start-App
    Cmd 105; Start-Sleep -Seconds 5      # Demarrer
    Shot (Main-Wnd) "$out\running.png"
    Cmd 106; Start-Sleep -Seconds 2      # Annuler
    Stop-App $p

    # 4. rendu termine, puis les fenetres
    Write-Config 3; Clear-Videos
    [IO.File]::WriteAllText("$drive\queue.txt", (($examples -join "`n") + "`n"))
    $p = Start-App
    Cmd 105; Start-Sleep -Seconds 20
    Shot (Main-Wnd) "$out\done.png"
    Cmd 157; Start-Sleep -Seconds 2
    Shot ([Cap]::FindWindow("ShaderToMP4About", [NullString]::Value)) "$out\about.png"
    Close-Wnd "ShaderToMP4About"
    Cmd 160; Start-Sleep -Seconds 2
    Shot ([Cap]::FindWindow("ShaderToMP4Output", [NullString]::Value)) "$out\output.png"
    Close-Wnd "ShaderToMP4Output"
    Cmd 168; Start-Sleep -Seconds 2
    Shot ([Cap]::FindWindow("ShaderToMP4Help", [NullString]::Value)) "$out\help.png"
    Close-Wnd "ShaderToMP4Help"
    Cmd 169; Start-Sleep -Seconds 3
    Shot ([Cap]::FindWindow("ShaderToMP4Gallery", [NullString]::Value)) "$out\examples.png"
    Close-Wnd "ShaderToMP4Gallery"
    Cmd 162; Start-Sleep -Seconds 3
    $ed = [Cap]::FindWindow("ShaderToMP4Editor", [NullString]::Value)
    if ($ed -ne [IntPtr]::Zero) { Shot $ed "$out\editor.png"; Close-Wnd "ShaderToMP4Editor" }
    # projets, file de travaux, historique, oscilloscopes, comparaison A/B (menu et palette : photographies de l'ecran, la fenetre principale les contient)
    Cmd 148; Start-Sleep -Seconds 3
    Cmd $ID.ID_PROJECT; Start-Sleep -Seconds 1
    Grab (Main-Wnd) "$out\project-menu.png"
    [System.Windows.Forms.SendKeys]::SendWait("{ESC}"); Start-Sleep -Milliseconds 500
    Cmd $ID.ID_PALETTE; Start-Sleep -Seconds 1
    [System.Windows.Forms.SendKeys]::SendWait("pr"); Start-Sleep -Milliseconds 600
    Shot ([Cap]::FindWindow("ShaderToMP4Palette", [NullString]::Value)) "$out\palette.png"
    [System.Windows.Forms.SendKeys]::SendWait("{ESC}"); Start-Sleep -Milliseconds 500
    Cmd $ID.ID_HISTORY; Start-Sleep -Seconds 2
    Shot ([Cap]::FindWindow("ShaderToMP4History", [NullString]::Value)) "$out\history.png"
    Close-Wnd "ShaderToMP4History"
    Cmd $ID.ID_SCOPES; Start-Sleep -Seconds 2
    Shot ([Cap]::FindWindow("ShaderToMP4Scopes", [NullString]::Value)) "$out\scopes.png"
    Close-Wnd "ShaderToMP4Scopes"
    Cmd $ID.ID_FREEZE; Start-Sleep -Milliseconds 400
    $prev = [IntPtr]::Zero; $last = [IntPtr]::Zero
    while (($sl = [Cap]::FindWindowEx((Main-Wnd), $prev, "STSlider", [NullString]::Value)) -ne [IntPtr]::Zero) { $prev = $sl; $last = $sl }
    [void][Cap]::SendMessage($last, 0x405, [IntPtr]1, [IntPtr]6000)   # TBM_SETPOS : un autre instant du shader
    Cmd 148; Start-Sleep -Seconds 4
    Shot (Main-Wnd) "$out\compare.png"
    Cmd $ID.ID_FREEZE; Start-Sleep -Milliseconds 300
    Stop-App $p

    # 5. visite guidee du premier lancement (la carte de la visite est une fenetre a part, posee sur la fenetre principale)
    Write-Ini $l 1
    Remove-Item "$drive\queue.txt" -ErrorAction SilentlyContinue
    $p = Start-App
    $m = Main-Wnd; [void][Cap]::SetForegroundWindow($m); Start-Sleep -Milliseconds 900
    Grab $m "$out\tour.png"
    Stop-App $p
    Write-Output "$l : captures dans $out"
  }
} finally {
  Get-Process ShaderToMP4 -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$drive\*" } | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 500
  subst $drive /D
  Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
