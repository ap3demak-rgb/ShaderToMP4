# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Controle du depot avant commit (hook Git) et en integration continue. Code de sortie 0 = tout est conforme.
.DESCRIPTION
  1. Anonymat : aucun terme interdit dans les fichiers suivis, les messages de commit, les auteurs et co-auteurs.
  2. Licence : en-tete SPDX dans chaque fichier source ; LICENSE et COPYING identiques ; documents de travail non suivis.
  3. Internationalisation : memes cles dans lang/en.json et lang/fr.json, toutes les cles utilisees existent,
     aucun texte visible ecrit en dur dans les sources.
  4. Documentation : chaque .md existe en anglais (X.md) et en francais (X.fr.md), sauf le fichier de feuille de route.
  5. Portabilite : aucune API du registre ni de dossier utilisateur dans les sources ; aucun binaire suivi.
#>
param([switch]$Quiet, [string]$MessageFile = "")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$enc = New-Object System.Text.UTF8Encoding($false)
$failures = New-Object System.Collections.Generic.List[string]
function Fail([string]$msg) { $script:failures.Add($msg) }
function Section([string]$t) { if (-not $Quiet) { Write-Host "== $t" } }

# Les termes interdits sont assembles ici a partir de fragments : ce script ne les contient pas en clair.
$forbidden = @(
  ('cl' + 'aude'), ('anthr' + 'opic'), ('chat' + 'gpt'), ('ope' + 'nai'), ('cop' + 'ilot'), ('gem' + 'ini'),
  ('\bA' + 'I\b'), ('\bI' + 'A\b'), ('assist' + 'ant'), ('\bL' + 'LM\b'), ('artificial ' + 'intelligence'),
  ('intelligence ' + 'artificielle'), ('co-authored' + '-by'), ('generated ' + 'by'), ('g\wn\wr\w ' + 'par')
)
$pattern = ($forbidden -join "|")
# Mode "message de commit" (hook commit-msg) : on ne controle que le texte du message.
if ($MessageFile) {
  $msg = [System.IO.File]::ReadAllText($MessageFile, $enc)
  $hit = [regex]::Match($msg, $pattern, "IgnoreCase")
  if ($hit.Success) { Write-Host "Message de commit refuse : terme interdit '$($hit.Value)'" -ForegroundColor Red; exit 1 }
  exit 0
}

$tracked = @(git ls-files)
$binaryExt = ".png", ".gif", ".ico", ".ref", ".jpg", ".jpeg", ".ttf"

# --- 1. Anonymat ---------------------------------------------------------------------------------
Section "Anonymat"
foreach ($f in $tracked) {
  if ($binaryExt -contains ([System.IO.Path]::GetExtension($f).ToLower())) { continue }
  if (-not (Test-Path $f)) { continue }
  $text = [System.IO.File]::ReadAllText((Resolve-Path $f), $enc)
  $m = [regex]::Match($text, $pattern, "IgnoreCase")
  if ($m.Success) { Fail "terme interdit dans $f : '$($m.Value)'" }
}
$meta = git log --all --format="%an%n%ae%n%cn%n%ce%n%s%n%b" 2>$null
foreach ($line in $meta) {
  if ($line -and [regex]::IsMatch($line, $pattern, "IgnoreCase")) { Fail "terme interdit dans l'historique Git : '$line'" }
}
foreach ($bad in @("ROADMAP.md", ("CL" + "AUDE" + ".md"))) {
  if ($tracked -contains $bad) { Fail "$bad ne doit pas etre suivi par Git" }
}

# --- 2. Licence ----------------------------------------------------------------------------------
Section "Licence"
$sourceExt = ".cpp", ".h", ".rc", ".bat", ".manifest", ".svg", ".glsl", ".ps1", ".sh"
foreach ($f in $tracked) {
  if ($sourceExt -notcontains ([System.IO.Path]::GetExtension($f).ToLower())) { continue }
  $head = (Get-Content $f -TotalCount 6 -Encoding UTF8) -join "`n"
  if ($head -notmatch "SPDX-License-Identifier: GPL-3.0-or-later") { Fail "en-tete SPDX manquant : $f" }
  elseif ($head -notmatch "Copyright \(C\) 2026 Patrick JAILLET") { Fail "ligne de copyright manquante : $f" }
}
foreach ($need in "LICENSE", "COPYING", "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.fr.md") {
  if ($tracked -notcontains $need) { Fail "fichier obligatoire absent : $need" }
}
if ((Test-Path LICENSE) -and (Test-Path COPYING)) {
  if ((Get-FileHash LICENSE).Hash -ne (Get-FileHash COPYING).Hash) { Fail "LICENSE et COPYING doivent etre identiques" }
}
if ((Test-Path .gitignore) -and ((Get-Content .gitignore -Raw) -match ("ROADMAP|CL" + "AUDE"))) { Fail ".gitignore ne doit pas nommer les documents de travail (utiliser .git/info/exclude)" }

# --- 3. Internationalisation ---------------------------------------------------------------------
Section "Internationalisation"
$en = [System.IO.File]::ReadAllText((Resolve-Path "lang/en.json"), $enc) | ConvertFrom-Json
$fr = [System.IO.File]::ReadAllText((Resolve-Path "lang/fr.json"), $enc) | ConvertFrom-Json
$enKeys = @($en.PSObject.Properties.Name); $frKeys = @($fr.PSObject.Properties.Name)
foreach ($k in $enKeys) { if ($frKeys -notcontains $k) { Fail "cle absente de fr.json : $k" } }
foreach ($k in $frKeys) { if ($enKeys -notcontains $k) { Fail "cle absente de en.json : $k" } }
foreach ($k in $enKeys) {
  # memes marques de format dans les deux langues (nombre et types), pour eviter un plantage a l'affichage
  $a = ([regex]::Matches($en.$k, "%[0-9$]*[.0-9]*(ll|l)?[dsfgux]")).Count
  $b = ([regex]::Matches($fr.$k, "%[0-9$]*[.0-9]*(ll|l)?[dsfgux]")).Count
  if ($a -ne $b) { Fail "nombre de marques de format different pour la cle $k" }
}
$srcFiles = Get-ChildItem src -Recurse -Include *.cpp, *.h
$used = @{}
foreach ($f in $srcFiles) {
  $text = Get-Content $f.FullName -Raw -Encoding UTF8
  foreach ($m in [regex]::Matches($text, '"((?:ui|log|error|dialog|status|dl|selftest|ini|about|cli|diag)\.[a-z0-9_.]+)"')) { $used[$m.Groups[1].Value] = $f.Name }
}
foreach ($k in $used.Keys) { if ($enKeys -notcontains $k -and $k -notmatch "\.h$|\.cpp$") { Fail "cle utilisee mais absente de lang/en.json : $k ($($used[$k]))" } }
foreach ($k in $enKeys) { if (-not $used.ContainsKey($k)) { Fail "cle de langue jamais utilisee : $k" } }
# Texte visible ecrit en dur : deux mots separes par une espace dans un litteral large, hors arguments ffmpeg, adresses et noms techniques.
$allowLine = '-hide_banner|-pix_fmt|-f rawvideo|-vf|-map|-c:v|-ss |-movflags|-nostdin|-framerate|-preset|-rc |-quality|-global_quality|-tag:v|-colorspace|nullsrc|tar\.exe|fade=|http|Segoe UI|Cascadia|DarkMode|^\s*//|#include|#pragma|PASS|FAIL|UPDATED|i18n-bootstrap|nom-technique|NVIDIA NVENC|APP_NAME_WSTR L" "|L" \(|L"\) ?"|%ls|%d|%02d|%05'
foreach ($f in $srcFiles) {
  $n = 0
  foreach ($line in (Get-Content $f.FullName -Encoding UTF8)) {
    $n++
    if ($line -match $allowLine) { continue }
    if ($line -match 'L"[^"]*\p{L}{3,} \p{L}{2,}[^"]*"') { Fail "texte visible en dur ($($f.Name):$n) : $($line.Trim())" }
  }
}

# --- 4. Documentation bilingue --------------------------------------------------------------------
Section "Documentation"
$docs = $tracked | Where-Object { $_ -like "*.md" }
foreach ($d in $docs) {
  if ($d -like "*.fr.md") {
    $en = $d -replace "\.fr\.md$", ".md"
    if ($docs -notcontains $en) { Fail "version anglaise manquante pour $d" }
  } else {
    $frd = $d -replace "\.md$", ".fr.md"
    if ($docs -notcontains $frd) { Fail "version francaise manquante pour $d" }
  }
}

# --- 5. Portabilite -------------------------------------------------------------------------------
Section "Portabilite"
$banned = 'RegOpenKey|RegCreateKey|RegSetValue|RegQueryValue|RegDeleteKey|RegDeleteValue|SHGetFolderPath|SHGetKnownFolderPath|CSIDL_|APPDATA|SHGetSpecialFolder'
foreach ($f in $srcFiles) {
  $n = 0
  # La lecture des variables d'environnement (PATH, LANG) est reservee a la couche plateforme POSIX
  $bannedHere = if ($f.FullName -match 'platform[\\/]posix') { $banned } else { $banned + '|getenv\(|_wgetenv' }
  foreach ($line in (Get-Content $f.FullName -Encoding UTF8)) { $n++; if ($line -match $bannedHere -and $line -notmatch '^\s*//') { Fail "acces hors du dossier de l'application ($($f.Name):$n) : $($line.Trim())" } }
}
foreach ($f in $tracked) {
  if ($f -match "\.(exe|dll|obj|lib|pdb|msi|dmg|pkg)$") { Fail "binaire suivi par Git : $f" }
  if ((Test-Path $f) -and ((Get-Item $f).Length -gt 50MB)) { Fail "fichier trop gros : $f" }
}

if ($failures.Count -gt 0) {
  Write-Host ""
  Write-Host "$($failures.Count) probleme(s) :" -ForegroundColor Red
  $failures | ForEach-Object { Write-Host "  - $_" }
  exit 1
}
if (-not $Quiet) { Write-Host "Depot conforme." -ForegroundColor Green }
exit 0
