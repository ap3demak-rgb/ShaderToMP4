# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Essai de la file de travaux de la fenetre : priorite, suspension, reordonnancement pendant un rendu, echec, annulation, historique, journal par travail.
.DESCRIPTION
  Copie bin\ShaderToMP4.exe et ffmpeg dans bin-queue\ (ignore par Git), y ecrit des shaders courts et un shader trop long, lance la fenetre et la pilote par
  messages de commande (identifiants lus dans src\gui\app.h) et par LVM_SETITEMSTATE pour choisir les lignes. Verifie ensuite history.jsonl, jobs\logs\, les
  fichiers produits et queue.txt. Ne touchez ni a la souris ni au clavier pendant l'essai (une minute environ). Code de sortie 0 = tout passe.
  Usage : .\scripts\test-queue.ps1
#>
param([switch]$Keep)  # -Keep : garde le dossier de travail pour comprendre un echec
Add-Type -AssemblyName System.Windows.Forms
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Q {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("kernel32.dll")] public static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr addr, UIntPtr size, uint type, uint prot);
  [DllImport("kernel32.dll")] public static extern bool VirtualFreeEx(IntPtr p, IntPtr addr, UIntPtr size, uint type);
  [DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(IntPtr p, IntPtr addr, byte[] buf, UIntPtr size, out UIntPtr written);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr p, IntPtr addr, byte[] buf, UIntPtr size, out UIntPtr read);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int d, int e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

# identifiants des commandes : relus dans l'enumeration de app.h
$hdr = Get-Content "$root\src\gui\app.h" -Raw
$enum = [regex]::Match($hdr, 'enum \{\s*ID_LIST = 101(.*?)\n\};', 'Singleline').Value -replace '//[^\n]*', ''
$ID = @{}
$next = 101
foreach ($tok in ($enum -replace '^enum \{', '' -replace '\};$', '') -split ',') {
  $t = $tok.Trim(); if (-not $t) { continue }
  if ($t -match '^(ID_[A-Z0-9_]+)\s*=\s*(\d+)$') { $next = [int]$Matches[2]; $ID[$Matches[1]] = $next }
  elseif ($t -match '^(ID_[A-Z0-9_]+)$') { $ID[$Matches[1]] = $next }
  else { continue }
  $next++
}
foreach ($k in "ID_START", "ID_CANCEL", "ID_HOLD", "ID_PRIO_UP", "ID_MOVE_UP", "ID_MOVE_DOWN", "ID_RENDER_SEL", "ID_PROJ_SAVE", "ID_JOBLOG") { if (-not $ID.ContainsKey($k)) { throw "identifiant $k introuvable dans app.h" } }

$work = Join-Path $root "bin-queue"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force "$work\shaders", "$work\videos" | Out-Null
Copy-Item "$root\bin\ShaderToMP4.exe" $work
foreach ($d in "lang") { if (Test-Path "$root\bin\$d") { Copy-Item "$root\bin\$d" "$work\$d" -Recurse } }
if (Test-Path "$root\bin\ffmpeg.exe") { Copy-Item "$root\bin\ffmpeg.exe" $work } else { throw "ffmpeg.exe absent de bin\ : lancer --get-ffmpeg" }

$short = "void mainImage(out vec4 o, in vec2 p) { o = vec4(p / iResolution.xy, 0.5 + 0.5 * sin(iTime), 1.0); }`n"
function New-Shader($name, $extra, $body) {
  $text = "// fps=10`n// width=64`n// height=64`n// duration=1`n$extra" + $body
  [IO.File]::WriteAllText("$work\shaders\$name.glsl", $text)
}
$long = "// duration=600`n// fps=30`n// width=640`n// height=360`n"
foreach ($n in "a_ok", "c_ok", "e_ok", "f_held", "g_ok", "h_ok", "i_ok") { New-Shader $n "" $short }
New-Shader "k_ok" "// crf=0`n" $short   # video sans perte : deux rendus identiques donnent les memes octets
New-Shader "b_broken" "" "void mainImage(out vec4 o, in vec2 p) { o = vec4(undefined_name); }`n"
[IO.File]::WriteAllText("$work\shaders\d_long.glsl", $long + $short)
[IO.File]::WriteAllText("$work\shaders\y_long.glsl", $long + $short)
[IO.File]::WriteAllText("$work\config.ini", "width=64`nheight=64`nfps=10`nduration=1`ncodec=h264`ncontainer=mp4`ncrf=28`npreset=ultrafast`nencoder=cpu`nauddio=`noutdir=$work\videos`nexists=overwrite`nchunk=0`n".Replace("auddio", "audio"))
[IO.File]::WriteAllText("$work\window.ini", "x=60 y=30 w=1016 h=939 max=0`nlang=en`ntour=0`n")

$fails = 0
function Check($ok, $what) { if ($ok) { Write-Output "PASS  $what" } else { Write-Output "FAIL  $what"; $script:fails++ } }
function Main-Wnd { [Q]::FindWindow("ShaderToMP4Main", [NullString]::Value) }
function Cmd($id) { [void][Q]::PostMessage((Main-Wnd), 0x111, [IntPtr]$id, [IntPtr]::Zero) }
function Select-Row($row) {  # une seule ligne selectionnee dans la liste (LVM_SETITEMSTATE avec la structure ecrite dans le processus de la fenetre)
  $lv = [Q]::FindWindowEx((Main-Wnd), [IntPtr]::Zero, "SysListView32", [NullString]::Value)
  [uint32]$pid_ = 0; [void][Q]::GetWindowThreadProcessId($lv, [ref]$pid_)
  $proc = [Q]::OpenProcess(0x0038, $false, $pid_)  # operation, ecriture, lecture memoire
  $mem = [Q]::VirtualAllocEx($proc, [IntPtr]::Zero, [UIntPtr][uint64]88, 0x3000, 4)
  function SetState($index, $state) {
    $b = New-Object byte[] 88
    [BitConverter]::GetBytes([uint32]($state)).CopyTo($b, 12)
    [BitConverter]::GetBytes([uint32]3).CopyTo($b, 16)  # LVIS_FOCUSED | LVIS_SELECTED
    [UIntPtr]$w = [UIntPtr]::Zero
    [void][Q]::WriteProcessMemory($proc, $mem, $b, [UIntPtr][uint64]88, [ref]$w)
    [void][Q]::SendMessage($lv, 0x102B, [IntPtr]$index, $mem)
  }
  SetState (-1) 0  # toute la selection est retiree
  SetState $row 3
  [void][Q]::VirtualFreeEx($proc, $mem, [UIntPtr]::Zero, 0x8000)
  [void][Q]::CloseHandle($proc)
  Start-Sleep -Milliseconds 250
}
function Row-Center($row) {  # centre d'une ligne de la liste, en coordonnees ecran (LVM_GETITEMRECT, resultat relu dans le processus de la fenetre)
  $lv = [Q]::FindWindowEx((Main-Wnd), [IntPtr]::Zero, "SysListView32", [NullString]::Value)
  [uint32]$pid_ = 0; [void][Q]::GetWindowThreadProcessId($lv, [ref]$pid_)
  $proc = [Q]::OpenProcess(0x0038, $false, $pid_)
  $mem = [Q]::VirtualAllocEx($proc, [IntPtr]::Zero, [UIntPtr][uint64]16, 0x3000, 4)
  $b = New-Object byte[] 16
  [UIntPtr]$w = [UIntPtr]::Zero
  [void][Q]::WriteProcessMemory($proc, $mem, $b, [UIntPtr][uint64]16, [ref]$w)  # left = LVIR_BOUNDS (0)
  [void][Q]::SendMessage($lv, 0x100E, [IntPtr]$row, $mem)
  $r = New-Object byte[] 16
  [void][Q]::ReadProcessMemory($proc, $mem, $r, [UIntPtr][uint64]16, [ref]$w)
  [void][Q]::VirtualFreeEx($proc, $mem, [UIntPtr]::Zero, 0x8000)
  [void][Q]::CloseHandle($proc)
  $pt = New-Object Q+POINT
  $pt.X = 40 + [BitConverter]::ToInt32($r, 0); $pt.Y = ([BitConverter]::ToInt32($r, 4) + [BitConverter]::ToInt32($r, 12)) / 2
  [void][Q]::ClientToScreen($lv, [ref]$pt)
  return $pt
}
function Mouse-Click($pt) { [void][Q]::SetCursorPos($pt.X, $pt.Y); Start-Sleep -Milliseconds 150; [Q]::mouse_event(2, 0, 0, 0, 0); Start-Sleep -Milliseconds 80; [Q]::mouse_event(4, 0, 0, 0, 0); Start-Sleep -Milliseconds 250 }
function Mouse-Drag($from, $to) {
  [void][Q]::SetCursorPos($from.X, $from.Y); Start-Sleep -Milliseconds 200
  [Q]::mouse_event(2, 0, 0, 0, 0); Start-Sleep -Milliseconds 150
  $steps = 12
  for ($i = 1; $i -le $steps; $i++) { [void][Q]::SetCursorPos([int]($from.X + ($to.X - $from.X) * $i / $steps), [int]($from.Y + ($to.Y - $from.Y) * $i / $steps)); Start-Sleep -Milliseconds 40 }
  Start-Sleep -Milliseconds 200
  [Q]::mouse_event(4, 0, 0, 0, 0); Start-Sleep -Milliseconds 400
}
function Win-Open($class) { [Q]::FindWindow($class, [NullString]::Value) -ne [IntPtr]::Zero }
function Win-Close($class) { $h = [Q]::FindWindow($class, [NullString]::Value); if ($h -ne [IntPtr]::Zero) { [void][Q]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero); Start-Sleep -Milliseconds 500 } }
function Keys($k) { [void][Q]::SetForegroundWindow((Main-Wnd)); Start-Sleep -Milliseconds 300; [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 1200 }
function Launch($names) {
  [IO.File]::WriteAllText("$work\queue.txt", (($names | ForEach-Object { "$work\shaders\$_.glsl" }) -join "`n") + "`n")
  $p = Start-Process "$work\ShaderToMP4.exe" -PassThru
  Start-Sleep -Seconds 3
  return $p
}
function Stop-App($p) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }; Start-Sleep -Milliseconds 600 }
function Wait-For($cond, $seconds) { $end = (Get-Date).AddSeconds($seconds); while ((Get-Date) -lt $end) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 300 }; return $false }
function Get-Hist { if (Test-Path "$work\history.jsonl") { @(Get-Content "$work\history.jsonl" -Encoding UTF8 | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json }) } else { @() } }
function Leaf($p) { Split-Path $p -Leaf }

try {
  # --- 1. six travaux : priorite, suspension, echec, annulation -----------------------------------------------------------------------------
  # liste : a_ok, b_broken, c_ok, d_long, e_ok (priorite haute), f_held (suspendu)
  $p = Launch @("a_ok", "b_broken", "c_ok", "d_long", "e_ok", "f_held")
  Select-Row 5; Cmd $ID.ID_HOLD; Start-Sleep -Milliseconds 400       # f suspendu
  Select-Row 4; Cmd $ID.ID_PRIO_UP; Start-Sleep -Milliseconds 400    # e en priorite haute
  Cmd $ID.ID_START
  $dlog = { @(Get-ChildItem "$work\jobs\logs\d_long-*.log" -ErrorAction SilentlyContinue).Count -gt 0 }
  Check (Wait-For $dlog 40) "le travail long demarre apres les travaux plus courts"
  Start-Sleep -Seconds 1
  Cmd $ID.ID_CANCEL
  $done = { (Get-Hist).Count -ge 5 }
  Check (Wait-For $done 30) "les cinq travaux de la file ont une ligne d'historique"
  Start-Sleep -Seconds 1
  $h = @(Get-Hist)  # ordre d'ecriture du fichier
  $names = $h | ForEach-Object { (Leaf $_.shader) }
  Check ($names[0] -eq "e_ok.glsl") "la priorite haute passe en premier (ordre : $($names -join ', '))"
  Check ((($names | Select-Object -Skip 1 -First 4) -join ",") -eq "a_ok.glsl,b_broken.glsl,c_ok.glsl,d_long.glsl") "les autres suivent l'ordre de la liste"
  $res = @{}; foreach ($e in $h) { $res[(Leaf $e.shader)] = $e.result }
  Check ($res["e_ok.glsl"] -eq "ok" -and $res["a_ok.glsl"] -eq "ok" -and $res["c_ok.glsl"] -eq "ok") "les travaux valides sont termines"
  Check ($res["b_broken.glsl"] -eq "failed") "le shader casse echoue"
  Check ($res["d_long.glsl"] -eq "cancelled") "le travail long est annule"
  Check (-not $res.ContainsKey("f_held.glsl")) "le travail suspendu n'est pas rendu"
  Check ((Test-Path "$work\videos\a_ok.mp4") -and (Test-Path "$work\videos\c_ok.mp4") -and (Test-Path "$work\videos\e_ok.mp4") -and -not (Test-Path "$work\videos\f_held.mp4")) "les videos des travaux termines existent, pas celle du travail suspendu"
  $logs = @(Get-ChildItem "$work\jobs\logs\*.log" | ForEach-Object { $_.Name })
  Check (($logs | Where-Object { $_ -like "b_broken-*" }).Count -eq 1 -and ($logs | Where-Object { $_ -like "f_held-*" }).Count -eq 0 -and $logs.Count -eq 5) "un journal par travail rendu (5 fichiers, aucun pour le suspendu)"
  $bl = Get-Content (Get-ChildItem "$work\jobs\logs\b_broken-*.log")[0].FullName -Raw
  Check ($bl -match "undefined_name") "le journal du travail en echec contient l'erreur du shader"
  $al = Get-Content (Get-ChildItem "$work\jobs\logs\a_ok-*.log")[0].FullName -Raw
  Check ($al -notmatch "undefined_name") "le journal d'un travail ne contient pas les lignes des autres"
  # reprise du travail suspendu : il est rendu a la relance
  Select-Row 5; Cmd $ID.ID_HOLD; Start-Sleep -Milliseconds 400
  Select-Row 5
  Cmd $ID.ID_RENDER_SEL
  $resumed = Wait-For { @(Get-Hist | Where-Object { (Leaf $_.shader) -eq "f_held.glsl" }).Count -ge 1 } 40
  if (-not $resumed) { Write-Output ("  historique : " + ((Get-Hist | ForEach-Object { (Leaf $_.shader) + "=" + $_.result }) -join ", ")) }
  Check $resumed "le travail repris est rendu"
  $q = if (Test-Path "$work\queue.txt") { Get-Content "$work\queue.txt" } else { @() }
  Stop-App $p

  # --- 2. reordonner pendant un rendu : le travail suivant suit le nouvel ordre -------------------------------------------------------------
  Remove-Item "$work\history.jsonl", "$work\jobs\logs" -Recurse -Force -ErrorAction SilentlyContinue
  $p = Launch @("y_long", "g_ok", "h_ok", "i_ok")
  Cmd $ID.ID_START
  Check (Wait-For { @(Get-ChildItem "$work\jobs\logs\y_long-*.log" -ErrorAction SilentlyContinue).Count -gt 0 } 30) "le premier travail (long) tourne"
  Select-Row 3; Cmd $ID.ID_MOVE_UP; Start-Sleep -Milliseconds 400; Cmd $ID.ID_MOVE_UP; Start-Sleep -Milliseconds 400  # i_ok remonte devant g_ok et h_ok
  $qtxt = Get-Content "$work\queue.txt" -ErrorAction SilentlyContinue
  Check (($qtxt | ForEach-Object { Leaf $_ }) -join "," -eq "y_long.glsl,i_ok.glsl,g_ok.glsl,h_ok.glsl") "la file notee dans queue.txt suit le nouvel ordre"
  Cmd $ID.ID_CANCEL
  Wait-For { (Get-Hist).Count -ge 1 } 20 | Out-Null
  Start-Sleep -Seconds 1
  Stop-App $p
  # relance : l'ordre est conserve, et le rendu des trois courts se fait dans cet ordre
  Remove-Item "$work\history.jsonl" -ErrorAction SilentlyContinue
  $p = Start-Process "$work\ShaderToMP4.exe" -PassThru
  Start-Sleep -Seconds 3
  Select-Row 0; Cmd $ID.ID_HOLD; Start-Sleep -Milliseconds 300  # y_long suspendu : seuls les trois courts sont rendus
  Cmd $ID.ID_START
  Check (Wait-For { (Get-Hist).Count -ge 3 } 40) "les trois travaux courts sont rendus apres le redemarrage"
  $order = (Get-Hist | ForEach-Object { Leaf $_.shader }) -join ","
  Check ($order -eq "i_ok.glsl,g_ok.glsl,h_ok.glsl") "l'ordre choisi est suivi (ordre : $order)"
  Stop-App $p

  # --- 3. projet : ouverture par la ligne de commande, enregistrement, priorite et reglages propres retrouves, rendu ---------------------------------------
  Remove-Item "$work\history.jsonl", "$work\queue.txt", "$work\jobs" -Recurse -Force -ErrorAction SilentlyContinue
  $proj = @{ format = "ShaderToMP4 project"; version = 1; name = "essai"; general = @("width=64", "height=64", "fps=10", "duration=1", "codec=h264", "container=mp4", "preset=ultrafast", "encoder=cpu", "outdir=videos", "exists=overwrite")
             shaders = @(@{ path = "shaders/g_ok.glsl"; batch = 2; priority = 1 }, @{ path = "shaders/h_ok.glsl"; settings = @("fps=20") }, @{ path = "shaders/i_ok.glsl"; priority = -1 }) }
  [IO.File]::WriteAllText("$work\essai.s2mproj", ($proj | ConvertTo-Json -Depth 5))
  $p = Start-Process "$work\ShaderToMP4.exe" -ArgumentList "`"$work\essai.s2mproj`"" -PassThru
  Start-Sleep -Seconds 3
  $p.Refresh()
  Check ($p.MainWindowTitle -like "*essai*") "le titre de la fenetre porte le nom du projet ($($p.MainWindowTitle))"
  Cmd $ID.ID_PROJ_SAVE; Start-Sleep -Seconds 1
  $saved = Get-Content "$work\essai.s2mproj" -Raw | ConvertFrom-Json
  Check ($saved.shaders.Count -eq 3 -and $saved.shaders[0].batch -eq 2 -and $saved.shaders[0].priority -eq 1 -and $saved.shaders[2].priority -eq -1) "l'enregistrement garde les shaders, le lot et les priorites"
  Check (($saved.shaders[1].settings -contains "fps=20") -and ($saved.general -contains "outdir=videos") -and ($saved.shaders[0].path -eq "shaders/g_ok.glsl")) "reglages propres, dossier de sortie et chemins relatifs conserves"
  Cmd $ID.ID_START
  Check (Wait-For { @(Get-Hist).Count -ge 3 } 40) "le projet ouvert est rendu"
  $order = (Get-Hist | ForEach-Object { Leaf $_.shader }) -join ","
  Check ($order -eq "g_ok.glsl,h_ok.glsl,i_ok.glsl") "la priorite du projet est appliquee (ordre : $order)"
  $hh = Get-Hist | Where-Object { (Leaf $_.shader) -eq "h_ok.glsl" }
  Check (($hh.settings -contains "fps=20") -and (Test-Path "$work\videos\h_ok.mp4")) "le shader garde ses reglages propres (fps=20) et sa video est produite dans le dossier du projet"
  Stop-App $p

  # --- 4. reordonner a la souris (glisser) et au clavier (Alt + fleches) -----------------------------------------------------------------------------
  Remove-Item "$work\history.jsonl", "$work\jobs" -Recurse -Force -ErrorAction SilentlyContinue
  $p = Launch @("a_ok", "c_ok", "e_ok", "g_ok")
  [void][Q]::SetForegroundWindow((Main-Wnd)); Start-Sleep -Milliseconds 400
  Mouse-Drag (Row-Center 3) (Row-Center 0)   # g_ok glisse au-dessus de a_ok
  $qtxt = (Get-Content "$work\queue.txt" | ForEach-Object { Leaf $_ }) -join ","
  Check ($qtxt -eq "g_ok.glsl,a_ok.glsl,c_ok.glsl,e_ok.glsl" -or $qtxt -eq "a_ok.glsl,g_ok.glsl,c_ok.glsl,e_ok.glsl") "glisser une ligne la deplace (ordre : $qtxt)"
  Mouse-Click (Row-Center 2)                 # e_ok est selectionne et la liste a le focus
  [System.Windows.Forms.SendKeys]::SendWait("%{UP}"); Start-Sleep -Milliseconds 500
  $qtxt2 = (Get-Content "$work\queue.txt" | ForEach-Object { Leaf $_ }) -join ","
  $before = ($qtxt -split ",")
  $expected = @($before[0], $before[2], $before[1], $before[3]) -join ","
  Check ($qtxt2 -eq $expected) "Alt+Haut monte la ligne selectionnee d'un cran (ordre : $qtxt2)"
  Mouse-Drag (Row-Center 0) (Row-Center 0)   # glisser sur place ne change rien
  $qtxt3 = (Get-Content "$work\queue.txt" | ForEach-Object { Leaf $_ }) -join ","
  Check ($qtxt3 -eq $qtxt2) "glisser sur place ne change rien"
  Stop-App $p

  # --- 5. raccourcis : keys.ini change un raccourci, un conflit est refuse, la palette lance une commande -----------------------------------------------
  Remove-Item "$work\queue.txt", "$work\keys.ini" -ErrorAction SilentlyContinue
  [IO.File]::WriteAllText("$work\keys.ini", "; essai`nhistory=Ctrl+Shift+Y`nscopes=Ctrl+Shift+Y`nbogus=F2`nfreeze=Hyper+X`n")
  $p = Start-Process "$work\ShaderToMP4.exe" -PassThru
  Start-Sleep -Seconds 3
  Keys "^+y"
  Check (Win-Open "ShaderToMP4History") "keys.ini : le nouveau raccourci (Ctrl+Maj+Y) ouvre l'historique"
  Win-Close "ShaderToMP4History"
  Keys "^h"
  Check (-not (Win-Open "ShaderToMP4History")) "keys.ini : l'ancien raccourci (Ctrl+H) ne fait plus rien"
  Keys "^+v"
  Check (Win-Open "ShaderToMP4Scopes") "keys.ini : le conflit est refuse, les oscilloscopes gardent Ctrl+Maj+V"
  Win-Close "ShaderToMP4Scopes"
  Keys "^k"
  Check (Win-Open "ShaderToMP4Palette") "Ctrl+K ouvre la palette de commandes"
  [System.Windows.Forms.SendKeys]::SendWait("scopes"); Start-Sleep -Milliseconds 500
  [System.Windows.Forms.SendKeys]::SendWait("{ENTER}"); Start-Sleep -Milliseconds 1500
  Check ((Win-Open "ShaderToMP4Scopes") -and -not (Win-Open "ShaderToMP4Palette")) "la palette lance la commande choisie (recherche sans accents ni casse) et se ferme"
  Win-Close "ShaderToMP4Scopes"
  Keys "^k"
  [System.Windows.Forms.SendKeys]::SendWait("{ESC}"); Start-Sleep -Milliseconds 600
  Check (-not (Win-Open "ShaderToMP4Palette")) "Echap ferme la palette"
  Stop-App $p

  # --- 6. historique : « Refaire ce rendu » donne le meme fichier, octet pour octet (video sans perte) ----------------------------------------------------
  Remove-Item "$work\history.jsonl", "$work\queue.txt", "$work\keys.ini", "$work\jobs" -Recurse -Force -ErrorAction SilentlyContinue
  $p = Launch @("k_ok")
  Cmd $ID.ID_START
  Check (Wait-For { @(Get-Hist).Count -ge 1 } 40) "le rendu sans perte est fait"
  Start-Sleep -Milliseconds 800
  $hash1 = (Get-FileHash "$work\videos\k_ok.mp4").Hash
  Remove-Item "$work\videos\k_ok.mp4" -Force
  Keys "^h"
  Check (Win-Open "ShaderToMP4History") "l'historique s'ouvre (Ctrl+H)"
  [System.Windows.Forms.SendKeys]::SendWait("{TAB} "); Start-Sleep -Milliseconds 500   # Tab : bouton « Refaire ce rendu », Espace : clic
  Check (Wait-For { @(Get-Hist).Count -ge 2 } 40) "« Refaire ce rendu » relance le rendu avec les memes reglages"
  Start-Sleep -Milliseconds 800
  $hist2 = @(Get-Hist)
  $hash2 = if (Test-Path "$work\videos\k_ok.mp4") { (Get-FileHash "$work\videos\k_ok.mp4").Hash } else { "" }
  Check ($hash1 -and $hash1 -eq $hash2) "le fichier refait est identique octet pour octet"
  Check ($hist2.Count -ge 2 -and $hist2[0].hash -eq $hist2[1].hash) "l'empreinte des reglages est la meme dans les deux lignes de l'historique"
  Stop-App $p
} finally {
  Get-Process ShaderToMP4 -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$work\*" } | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 500
  if (-not $Keep) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
}
if ($fails) { Write-Output "$fails controle(s) en echec."; exit 1 }
Write-Output "File de travaux : tout passe."
