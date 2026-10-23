# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Journal des performances d'une version : cadence du banc d'essai, temps d'ouverture de la fenetre, memoire d'un rendu de reference ; comparaison avec la version
  precedente (une regression de plus de 15 % bloque la version).
.DESCRIPTION
  Mesures (mediane de trois passes) : 1. `--benchmark --quick --json` (images par seconde par taille et encodeur) ; 2. temps entre le lancement et la fenetre
  principale visible (cinq lancements) ; 3. pic de memoire (working set et memoire privee) et duree d'un rendu de reference en ligne de commande (examples\plasma.glsl,
  1280x720, 30 images par seconde, 4 secondes, encodeur CPU). Le fichier dist\ShaderToMP4-<version>-perf.json est publie avec la version.
  -Previous <fichier> : compare cas par cas ; echec (code 1) si une cadence baisse de plus de -MaxDrop %, ou si le temps d'ouverture, la duree ou la memoire du rendu
  augmentent de plus de -MaxDrop %. La comparaison n'a de sens que sur le meme poste : si le moteur OpenGL ou le nombre de processeurs different, elle est
  sautee et le journal le dit (sur l'integration continue, les machines changent d'une execution a l'autre : l'alerte reste, le blocage ne vaut que sur un meme poste).
#>
param([string]$Exe = "bin\ShaderToMP4.exe", [string]$Previous = "", [double]$MaxDrop = 15, [string]$Out = "")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class PerfW { [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t); [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h); }
"@
function Median($a) { $s = @($a | Sort-Object); if ($s.Count -eq 0) { return 0 }; $s[[int][math]::Floor(($s.Count - 1) / 2)] }

$version = (Select-String -Path src\app_info.h -Pattern '^#define APP_VER_STR "(.*)"').Matches[0].Groups[1].Value
$exeFull = (Resolve-Path $Exe).Path
$tmp = Join-Path ([IO.Path]::GetTempPath()) "perf-journal"
New-Item -ItemType Directory -Force $tmp | Out-Null

# --- Poste : moteur OpenGL et processeurs (la comparaison ne vaut que sur le meme) ---
$diag = Join-Path $tmp "diag.txt"
Start-Process $exeFull -ArgumentList "--diagnostic" -Wait -NoNewWindow -RedirectStandardOutput $diag | Out-Null
$renderer = ((Get-Content $diag | Select-String "OpenGL" | Where-Object { $_.Line -match "renderer|Moteur|Renderer" } | Select-Object -First 1).Line -replace '^[^:]*:\s*', '')
$machine = [ordered]@{ renderer = $renderer; processors = [Environment]::ProcessorCount; os = [Environment]::OSVersion.VersionString }

# --- 1. Banc d'essai ---
$runs = @()
for ($i = 0; $i -lt 3; $i++) {
  $f = Join-Path $tmp "bench$i.json"
  $p = Start-Process $exeFull -ArgumentList "--benchmark", "--quick", "--seconds=2", "--json" -Wait -PassThru -NoNewWindow -RedirectStandardOutput $f
  if ($p.ExitCode -ne 0) { throw "benchmark : code de sortie $($p.ExitCode)" }
  $runs += , ((Get-Content $f -Raw | ConvertFrom-Json).results)
}
$bench = @()
foreach ($c in $runs[0]) {
  $vals = foreach ($r in $runs) { ($r | Where-Object { $_.width -eq $c.width -and $_.height -eq $c.height -and $_.encoder -eq $c.encoder }).fps }
  $bench += [ordered]@{ width = $c.width; height = $c.height; encoder = $c.encoder; fps = [math]::Round((Median $vals), 1) }
}

# --- 2. Temps d'ouverture ---
$ini = Join-Path (Split-Path $exeFull) "window.ini"
$keepIni = if (Test-Path $ini) { Get-Content $ini -Raw } else { $null }
Set-Content $ini "x=100 y=40 w=1000 h=800 max=0`nlang=en`ntour=0`n" -NoNewline   # sans visite guidee : on mesure la fenetre principale
$starts = @()
try {
  for ($i = 0; $i -lt 5; $i++) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process $exeFull -PassThru
    $ok = $false
    while ($sw.Elapsed.TotalSeconds -lt 15) {
      $h = [PerfW]::FindWindow("ShaderToMP4Main", [NullString]::Value)
      if ($h -ne [IntPtr]::Zero -and [PerfW]::IsWindowVisible($h)) { $ok = $true; break }
      Start-Sleep -Milliseconds 5
    }
    $ms = $sw.Elapsed.TotalMilliseconds
    Stop-Process -Id $p.Id -Force
    Start-Sleep -Milliseconds 300
    if (-not $ok) { throw "la fenetre principale n'est pas apparue en 15 s" }
    $starts += $ms
  }
} finally {
  if ($null -ne $keepIni) { Set-Content $ini $keepIni -NoNewline } else { Remove-Item $ini -ErrorAction SilentlyContinue }
}

# --- 3. Rendu de reference : duree et pic de memoire ---
$outDir = Join-Path $tmp "render"
$secs = @(); $ws = @(); $priv = @()
for ($i = 0; $i -lt 3; $i++) {
  if (Test-Path $outDir) { Remove-Item $outDir -Recurse -Force }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process $exeFull -ArgumentList "--render", "--width=1280", "--height=720", "--fps=30", "--duration=4", "--encoder=cpu", "--audio=", "--outdir=$outDir", "examples\plasma.glsl" -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $tmp "render.txt")
  $null = $p.Handle  # garde le descripteur : sans cela ExitCode reste vide quand on attend par HasExited
  $peakWs = 0; $peakPriv = 0
  while (-not $p.HasExited) {
    try { $p.Refresh(); $peakWs = [math]::Max($peakWs, $p.WorkingSet64); $peakPriv = [math]::Max($peakPriv, $p.PrivateMemorySize64) } catch {}
    Start-Sleep -Milliseconds 50
  }
  $p.WaitForExit()
  if ($p.ExitCode -ne 0) { throw "rendu de reference : code de sortie $($p.ExitCode)" }
  $secs += $sw.Elapsed.TotalSeconds; $ws += $peakWs / 1MB; $priv += $peakPriv / 1MB
}
$journal = [ordered]@{
  version = $version; date = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd"); machine = $machine
  benchmark = $bench; startup_ms = [math]::Round((Median $starts), 0)
  render = [ordered]@{ shader = "examples/plasma.glsl"; size = "1280x720"; fps = 30; seconds_of_video = 4; wall_seconds = [math]::Round((Median $secs), 2)
                       peak_working_set_mb = [math]::Round((Median $ws), 0); peak_private_mb = [math]::Round((Median $priv), 0) }
}
if (-not $Out) { New-Item -ItemType Directory -Force dist | Out-Null; $Out = "dist\ShaderToMP4-$version-perf.json" }
[IO.File]::WriteAllText((Join-Path (Get-Location) $Out), ($journal | ConvertTo-Json -Depth 6), (New-Object System.Text.UTF8Encoding($false)))
Write-Output ($journal | ConvertTo-Json -Depth 6)

# --- Comparaison avec la version precedente ---
if (-not $Previous) { exit 0 }
if (-not (Test-Path $Previous)) { Write-Output "Pas de journal precedent ($Previous) : rien a comparer."; exit 0 }
$old = Get-Content $Previous -Raw | ConvertFrom-Json
if ($old.machine.renderer -ne $machine.renderer -or [int]$old.machine.processors -ne $machine.processors) {
  Write-Output "Comparaison sautee : autre poste (precedent : $($old.machine.renderer), $($old.machine.processors) processeurs ; maintenant : $($machine.renderer), $($machine.processors))."
  exit 0
}
$bad = New-Object System.Collections.Generic.List[string]
foreach ($c in $bench) {
  $o = $old.benchmark | Where-Object { $_.width -eq $c.width -and $_.height -eq $c.height -and $_.encoder -eq $c.encoder } | Select-Object -First 1
  if ($o -and $o.fps -gt 0) {
    $change = 100 * ($c.fps - $o.fps) / $o.fps
    Write-Output ("{0}x{1} {2}: {3} -> {4} fps ({5:+0.0;-0.0} %)" -f $c.width, $c.height, $c.encoder, $o.fps, $c.fps, $change)
    if ($change -lt -$MaxDrop) { $bad.Add("cadence $($c.width)x$($c.height) $($c.encoder) : $($o.fps) -> $($c.fps) fps") }
  }
}
function Worse($name, $before, $after) {
  if ($before -gt 0) {
    $change = 100 * ($after - $before) / $before
    Write-Output ("{0}: {1} -> {2} ({3:+0.0;-0.0} %)" -f $name, $before, $after, $change)
    if ($change -gt $MaxDrop) { $bad.Add("$name : $before -> $after") }
  }
}
Worse "temps d'ouverture (ms)" $old.startup_ms $journal.startup_ms
Worse "duree du rendu de reference (s)" $old.render.wall_seconds $journal.render.wall_seconds
Worse "pic de memoire du rendu (Mo)" $old.render.peak_working_set_mb $journal.render.peak_working_set_mb
if ($bad.Count -gt 0) { Write-Output ""; Write-Output "REGRESSION de plus de $MaxDrop % :"; $bad | ForEach-Object { Write-Output "  $_" }; exit 1 }
Write-Output "Aucune regression de plus de $MaxDrop %."
