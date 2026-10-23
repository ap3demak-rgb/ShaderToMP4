# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Controle des vulnerabilites connues des composants tiers livres avec la version (base NVD du NIST, par nom CPE et version).
.DESCRIPTION
  Pour chaque composant de scripts\components.json qui a un nom CPE, interroge https://services.nvd.nist.gov (API 2.0, sans cle : cinq requetes
  par 30 secondes) et ecrit le resultat dans dist\ShaderToMP4-<version>-vulnerabilities.txt (publie avec la version par la CI). Seules les adresses du
  composant sont envoyees (son nom CPE) ; rien du programme ni de l'utilisateur.
  Codes de sortie : 0 aucune vulnerabilite connue trouvee, 1 au moins une trouvee (avec -Strict seulement : sinon 0, le rapport fait foi),
  2 la base n'a pas pu etre interrogee. Limites : la base ne connait que les versions que les fiches CVE nomment ; un resultat vide n'est pas une
  garantie, et le programme lui-meme (code propre) n'est pas couvert par une base de ce genre.
#>
param([switch]$Strict, [string]$Out = "", [string]$Api = "https://services.nvd.nist.gov/rest/json/cves/2.0")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)

$version = (Select-String -Path src\app_info.h -Pattern '^#define APP_VER_STR "(.*)"').Matches[0].Groups[1].Value
$inst = Get-Content scripts\package-installer.ps1 -Raw
$ffVersion = [regex]::Match($inst, '\$ffVersion = "([^"]+)"').Groups[1].Value
$list = (Get-Content scripts\components.json -Raw | ConvertFrom-Json).components | Where-Object { $_.cpe }
$report = New-Object System.Text.StringBuilder
[void]$report.AppendLine("ShaderToMP4 $version : known vulnerabilities of the third-party components (NVD, $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd')))")
$found = 0; $failed = 0; $first = $true
foreach ($c in $list) {
  $v = if ($c.id -eq "ffmpeg") { $ffVersion } else { $c.version }
  $cpe = $c.cpe -replace '\{version\}', $v
  if (-not $first) { Start-Sleep -Seconds 7 }  # limite de l'API sans cle
  $first = $false
  $uri = "$Api`?virtualMatchString=$([uri]::EscapeDataString($cpe))"
  $res = $null
  foreach ($try in 1, 2) {
    try { $res = Invoke-RestMethod -Uri $uri -UserAgent "ShaderToMP4-check-vulnerabilities" -TimeoutSec 60; break }
    catch { if ($try -eq 1) { Start-Sleep -Seconds 35 } else { [void]$report.AppendLine("$($c.name) $v : NVD not reachable ($($_.Exception.Message))"); $failed++ } }
  }
  if (-not $res) { continue }
  $n = [int]$res.totalResults
  if ($n -eq 0) { [void]$report.AppendLine("$($c.name) $v ($cpe): none found"); continue }
  $found += $n
  [void]$report.AppendLine("$($c.name) $v ($cpe): $n found")
  foreach ($item in $res.vulnerabilities) {
    $m = $item.cve.metrics.cvssMetricV31 | Select-Object -First 1
    $sev = if ($m) { "$($m.cvssData.baseSeverity) $($m.cvssData.baseScore)" } else { "no score" }
    [void]$report.AppendLine("  $($item.cve.id) ($sev) https://nvd.nist.gov/vuln/detail/$($item.cve.id)")
  }
}
[void]$report.AppendLine("")
[void]$report.AppendLine("Limits: the base only knows the versions that CVE records name; an empty result is not a guarantee, and the program's own code is not covered by this kind of base.")
if (-not $Out) { New-Item -ItemType Directory -Force dist | Out-Null; $Out = "dist\ShaderToMP4-$version-vulnerabilities.txt" }
[IO.File]::WriteAllText((Join-Path (Get-Location) $Out), $report.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Output $report.ToString()
if ($failed -gt 0) { exit 2 }
if ($found -gt 0 -and $Strict) { exit 1 }
exit 0
