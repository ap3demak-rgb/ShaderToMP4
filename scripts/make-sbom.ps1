# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Nomenclature logicielle (SBOM) de la version Windows, au format SPDX 2.3 (JSON) : le programme et les composants livres avec lui.
.DESCRIPTION
  Lit scripts\components.json (composants), la version du programme dans src\app_info.h, la version et la somme SHA-256 de ffmpeg dans
  scripts\package-installer.ps1 et, si bin\ShaderToMP4.exe existe, la somme de l'executable. Ecrit dist\ShaderToMP4-<version>-sbom.spdx.json.
  Le fichier est publie avec chaque version (CI) ; scripts\check-vulnerabilities.ps1 lit les memes composants.
#>
param([string]$Out = "")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)

$version = (Select-String -Path src\app_info.h -Pattern '^#define APP_VER_STR "(.*)"').Matches[0].Groups[1].Value
$inst = Get-Content scripts\package-installer.ps1 -Raw
$ffVersion = [regex]::Match($inst, '\$ffVersion = "([^"]+)"').Groups[1].Value
$ffSha = [regex]::Match($inst, '\$ffSha256 = "([0-9a-f]{64})"').Groups[1].Value
if (-not $ffVersion -or -not $ffSha) { throw "version ou somme de ffmpeg introuvable dans package-installer.ps1" }
$list = (Get-Content scripts\components.json -Raw | ConvertFrom-Json).components

$packages = New-Object System.Collections.Generic.List[object]
$rels = New-Object System.Collections.Generic.List[object]
$self = [ordered]@{
  SPDXID = "SPDXRef-ShaderToMP4"; name = "ShaderToMP4"; versionInfo = $version
  supplier = "Person: Patrick JAILLET"; downloadLocation = "https://github.com/ap3demak-rgb/ShaderToMP4/releases/tag/v$version"
  filesAnalyzed = $false; licenseConcluded = "GPL-3.0-or-later"; licenseDeclared = "GPL-3.0-or-later"
  copyrightText = "Copyright (C) 2026 Patrick JAILLET"; homepage = "https://ap3demak-rgb.github.io/ShaderToMP4"
}
if (Test-Path bin\ShaderToMP4.exe) { $self.checksums = @(@{ algorithm = "SHA256"; checksumValue = (Get-FileHash bin\ShaderToMP4.exe -Algorithm SHA256).Hash.ToLower() }) }
$packages.Add($self)
$rels.Add([ordered]@{ spdxElementId = "SPDXRef-DOCUMENT"; relationshipType = "DESCRIBES"; relatedSpdxElement = "SPDXRef-ShaderToMP4" })
foreach ($c in $list) {
  $v = if ($c.id -eq "ffmpeg") { $ffVersion } else { $c.version }
  $p = [ordered]@{
    SPDXID = "SPDXRef-" + $c.id; name = $c.name; versionInfo = $v; supplier = $c.supplier
    downloadLocation = ($c.downloadLocation -replace '\{version\}', $v); filesAnalyzed = $false
    licenseConcluded = $c.license; licenseDeclared = $c.license; copyrightText = "NOASSERTION"; comment = $c.comment
  }
  if ($c.id -eq "ffmpeg") { $p.checksums = @(@{ algorithm = "SHA256"; checksumValue = $ffSha }) }
  if ($c.cpe) { $p.externalRefs = @(@{ referenceCategory = "SECURITY"; referenceType = "cpe23Type"; referenceLocator = ($c.cpe -replace '\{version\}', $v) }) }
  $packages.Add($p)
  $rels.Add([ordered]@{ spdxElementId = "SPDXRef-ShaderToMP4"; relationshipType = $(if ($c.shippedIn -eq "installer") { "CONTAINED_BY" } else { "CONTAINS" }); relatedSpdxElement = "SPDXRef-" + $c.id })
}
# CONTAINED_BY va dans l'autre sens : « le composant est contenu dans l'installeur du programme »
$fixed = foreach ($r in $rels) {
  if ($r.relationshipType -eq "CONTAINED_BY") { [ordered]@{ spdxElementId = $r.relatedSpdxElement; relationshipType = "CONTAINED_BY"; relatedSpdxElement = "SPDXRef-ShaderToMP4" } } else { $r }
}
$doc = [ordered]@{
  spdxVersion = "SPDX-2.3"; dataLicense = "CC0-1.0"; SPDXID = "SPDXRef-DOCUMENT"; name = "ShaderToMP4-$version"
  documentNamespace = "https://ap3demak-rgb.github.io/ShaderToMP4/sbom/$version-" + [guid]::NewGuid().ToString()
  creationInfo = [ordered]@{ created = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ"); creators = @("Tool: ShaderToMP4-make-sbom.ps1", "Person: Patrick JAILLET") }
  packages = $packages.ToArray(); relationships = @($fixed)
}
if (-not $Out) { New-Item -ItemType Directory -Force dist | Out-Null; $Out = "dist\ShaderToMP4-$version-sbom.spdx.json" }
[IO.File]::WriteAllText((Join-Path (Get-Location) $Out), ($doc | ConvertTo-Json -Depth 8), (New-Object System.Text.UTF8Encoding($false)))
Write-Output $Out
