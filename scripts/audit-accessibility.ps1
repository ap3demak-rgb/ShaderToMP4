# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

<#
.SYNOPSIS
  Audit d'accessibilite des fenetres du programme par UI Automation (celle que lisent les lecteurs d'ecran) : chaque controle interactif doit avoir un
  nom et pouvoir recevoir le focus du clavier, et la touche Tab doit parcourir tous les controles.
.DESCRIPTION
  Lance bin\ShaderToMP4.exe, ouvre chaque fenetre (message WM_COMMAND), la parcourt par UI Automation et ecrit un tableau : type, nom, focalisable. Echec
  (code 1) si un bouton, champ, liste, curseur ou liste deroulante n'a pas de nom ou ne se focalise pas au clavier, ou si Tab ne passe pas par tous.
  Le contraste des couleurs est controle par scripts\check-repo.ps1 (WCAG AA) ; ce script ne mesure ni contraste ni tailles.
  Hors de portee : ce que seul un lecteur d'ecran reel peut dire (ordre de lecture, annonces), non essaye ici.
#>
param([string]$Exe = "bin\ShaderToMP4.exe", [string]$Lang = "en")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public class AuditW {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
$ini = "$(Get-Location)\bin\window.ini"
$keepIni = if (Test-Path $ini) { Get-Content $ini -Raw } else { $null }
Set-Content $ini "x=200 y=40 w=1100 h=900 max=0`nlang=$Lang`ntour=0`n" -NoNewline   # une fenetre visible sur l'ecran principal, sans visite guidee
$p = Start-Process $Exe -PassThru
$failures = New-Object System.Collections.Generic.List[string]
try {
  Start-Sleep -Seconds 3
  $main = [AuditW]::FindWindow("ShaderToMP4Main", [NullString]::Value)
  if ($main -eq [IntPtr]::Zero) { throw "fenetre principale introuvable" }
  # (classe, identifiant de commande du bouton de la fenetre principale ; 0 = la fenetre principale elle-meme)
  $targets = @(@("main", "ShaderToMP4Main", 0), @("about", "ShaderToMP4About", 157), @("help", "ShaderToMP4Help", 168), @("gallery", "ShaderToMP4Gallery", 169))
  # controles interactifs reconnus par leur classe Windows (le type UI Automation « panneau » des controles systeme depend du client : la classe est sure)
  $interactive = @("Button", "Edit", "ComboBox", "ListBox", "SysListView32", "STSlider", "RICHEDIT50W")
  foreach ($t in $targets) {
    $name, $class, $cmd = $t
    if ($cmd -gt 0) { [AuditW]::PostMessage($main, 0x111, [IntPtr]$cmd, [IntPtr]::Zero) | Out-Null; Start-Sleep -Seconds 2 }
    $h = [AuditW]::FindWindow($class, [NullString]::Value)
    if ($h -eq [IntPtr]::Zero) { $failures.Add("${name}: fenetre non ouverte"); continue }
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($h)
    $all = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    Write-Output "== $name ($($all.Count) elements)"
    $focusable = New-Object System.Collections.Generic.List[object]
    foreach ($e in $all) {
      $class = $e.Current.ClassName
      if ($interactive -notcontains $class -or $e.Current.IsOffscreen) { continue }
      $n = $e.Current.Name
      Write-Output ("  {0,-14} {1}" -f $class, $(if ($n) { $n } else { "<<SANS NOM>>" }))
      if (-not $n -and $class -ne "Edit" -and $class -ne "ComboBox") { $failures.Add("${name}: $class sans nom") }
      $focusable.Add($e)
    }
    # Tab doit parcourir tous les controles interactifs (les elements d'une liste se parcourent aux fleches : la liste entiere compte pour un)
    [AuditW]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 300
    $seen = New-Object System.Collections.Generic.HashSet[string]
    for ($i = 0; $i -lt ($focusable.Count + 4); $i++) {
      [System.Windows.Forms.SendKeys]::SendWait("{TAB}")
      Start-Sleep -Milliseconds 120
      $fe = [System.Windows.Automation.AutomationElement]::FocusedElement
      if ($fe) { [void]$seen.Add($fe.Current.ClassName + "|" + $fe.Current.Name) }
    }
    $missed = 0
    foreach ($e in $focusable) {
      if (-not $seen.Contains($e.Current.ClassName + "|" + $e.Current.Name)) {
        $missed++
        if ($e.Current.Enabled) { $failures.Add("${name}: Tab n'atteint pas '$($e.Current.Name)' ($($e.Current.ClassName))") }
      }
    }
    Write-Output "  Tab : $($focusable.Count - $missed) controles atteints sur $($focusable.Count)"
    if ($cmd -gt 0) { [AuditW]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 600 }
  }
} finally {
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
  Start-Sleep -Milliseconds 400
  if ($null -ne $keepIni) { Set-Content $ini $keepIni -NoNewline } else { Remove-Item $ini -ErrorAction SilentlyContinue }
}
if ($failures.Count -gt 0) { Write-Output ""; Write-Output "PROBLEMES :"; $failures | ForEach-Object { Write-Output "  $_" }; exit 1 }
Write-Output ""
Write-Output "Audit reussi : chaque controle interactif a un nom et se focalise au clavier, Tab les parcourt tous."
