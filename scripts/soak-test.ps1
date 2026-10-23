# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Essai de longue duree : UN SEUL processus rend des videos en parties (et en parallele) pendant -Minutes minutes ; le systeme mesure toutes les -Every secondes le
  nombre de descripteurs ouverts, la memoire privee et le working set du processus.
.DESCRIPTION
  Lance `ShaderToMP4 --render --watch=<dossier> --watch-move --chunk=1 --parallel=N` : le processus reste en vie et rend chaque copie de examples\plasma.glsl que ce
  script depose dans le dossier surveille (deux copies a la fois, pour que la file ne soit jamais vide). Les mesures viennent du systeme (Process.HandleCount,
  PrivateMemorySize64, WorkingSet64). La premiere moitie sert de mise en route ; ensuite le script echoue (code 1)
  si la mediane des descripteurs ou de la memoire privee de la derniere fenetre depasse de plus de -MaxGrowth % celle de la premiere de cette partie (trois fenetres), si le processus s'arrete avant la fin ou si un rendu echoue
  (dossier failed\). Les mesures sont ecrites en CSV (-Csv). Duree d'une verification complete : plusieurs heures (-Minutes 240) ; un essai de quelques minutes ne
  voit que les fuites rapides.
#>
param([int]$Minutes = 20, [int]$Every = 10, [int]$Parallel = 2, [double]$MaxGrowth = 5, [string]$Csv = "", [string]$Exe = "bin\ShaderToMP4.exe")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
$exeFull = (Resolve-Path $Exe).Path
$work = Join-Path ([IO.Path]::GetTempPath()) "soak-test"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
$in = Join-Path $work "in"
New-Item -ItemType Directory -Force $in, (Join-Path $work "out") | Out-Null
$shader = (Resolve-Path "examples\plasma.glsl").Path
$n = 0
function Feed {
  $pending = @(Get-ChildItem $in -Filter *.glsl -ErrorAction SilentlyContinue).Count
  while ($pending -lt 2) { Copy-Item $shader (Join-Path $in ("p{0:D6}.glsl" -f $script:n)); $script:n++; $pending++ }
}
Feed
$argsList = @("--render", "--watch=$in", "--watch-move", "--width=640", "--height=360", "--fps=30", "--duration=6", "--chunk=1", "--parallel=$Parallel", "--encoder=cpu", "--audio=", "--exists=number", "--outdir=$work\out")
if (-not $Csv) { New-Item -ItemType Directory -Force dist | Out-Null; $Csv = "dist\soak-test.csv" }
"seconds,handles,private_mb,working_set_mb" | Set-Content $Csv
$samples = New-Object System.Collections.Generic.List[object]
$start = Get-Date
$deadline = $start.AddMinutes($Minutes)
$died = $false
$p = Start-Process $exeFull -ArgumentList $argsList -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $work "out.txt")
$null = $p.Handle
try {
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds $Every
    if ($p.HasExited) { $died = $true; Write-Output "le processus s'est arrete avec le code $($p.ExitCode)"; break }
    Feed
    # le dossier de sortie ne garde que les derniers fichiers (le disque ne se remplit pas) : les plus anciens sont supprimes
    Get-ChildItem (Join-Path $work "out") -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -Skip 6 | Remove-Item -Force -ErrorAction SilentlyContinue
    try {
      $p.Refresh()
      $row = [pscustomobject]@{ t = ((Get-Date) - $start).TotalSeconds; handles = $p.HandleCount; priv = $p.PrivateMemorySize64 / 1MB; ws = $p.WorkingSet64 / 1MB }
      $samples.Add($row)
      [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0:F0},{1},{2:F1},{3:F1}", $row.t, $row.handles, $row.priv, $row.ws) | Add-Content $Csv
    } catch {}
  }
} finally {
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
}
$done = @(Get-ChildItem (Join-Path $in "done") -Filter *.glsl -ErrorAction SilentlyContinue).Count
$failed = @(Get-ChildItem (Join-Path $in "failed") -Filter *.glsl -ErrorAction SilentlyContinue).Count
Write-Output ("{0} mesures sur {1:N1} minutes ; {2} videos rendues, {3} echecs" -f $samples.Count, ((Get-Date) - $start).TotalMinutes, $done, $failed)
if ($samples.Count -lt 6) { Write-Output "trop peu de mesures pour conclure"; exit 1 }
# seconde moitie, en trois fenetres : mediane de la derniere contre mediane de la premiere (la memoire d'un rendu en parties monte et descend a chaque video : une droite
# ajustee a ces dents de scie donnerait n'importe quoi)
$half = @($samples | Where-Object { $_.t -ge $samples[$samples.Count - 1].t / 2 })
function Med($v) { $s = @($v | Sort-Object); $s[[int][math]::Floor(($s.Count - 1) / 2)] }
$w = [math]::Max(1, [int][math]::Floor($half.Count / 3))
$first = @($half | Select-Object -First $w); $last = @($half | Select-Object -Last $w)
$gh = 100.0 * ((Med ($last | ForEach-Object { [double]$_.handles })) - (Med ($first | ForEach-Object { [double]$_.handles }))) / (Med ($first | ForEach-Object { [double]$_.handles }))
$mp1 = Med ($first | ForEach-Object { $_.priv }); $mp3 = Med ($last | ForEach-Object { $_.priv })
$gp = 100.0 * ($mp3 - $mp1) / $mp1
Write-Output ("descripteurs : mediane {0} -> {1} ({2:+0.0;-0.0} %)" -f (Med ($first | ForEach-Object { $_.handles })), (Med ($last | ForEach-Object { $_.handles })), $gh)
Write-Output ("memoire privee : mediane {0:N0} -> {1:N0} Mo ({2:+0.0;-0.0} %) entre le debut et la fin de la seconde moitie" -f $mp1, $mp3, $gp)
if ($died -or $failed -gt 0 -or $gh -gt $MaxGrowth -or $gp -gt $MaxGrowth) { Write-Output "ECHEC : fuite probable ou rendu en erreur (limite de croissance $MaxGrowth %)"; exit 1 }
Write-Output "Aucune croissance au-dela de $MaxGrowth % sur la seconde moitie."
