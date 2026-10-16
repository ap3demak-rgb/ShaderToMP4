; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Patrick JAILLET
;
; Installeur Windows de ShaderToMP4 (NSIS 3, Modern UI 2). A construire par scripts\package-installer.ps1, qui passe les defines
; (APP_NAME, APP_VERSION, APP_VER4, APP_PUBLISHER, APP_URL, OUTFILE, STAGE, GEN, ICON, LICENSEFILE, SIZE_KB) et genere dans GEN :
;   strings.nsh         textes de l'application (lang/*.json, cles installer.*) pour les neuf langues
;   files_install.nsh   une ligne File par fichier du dossier STAGE (hors ffmpeg.exe, traite a part)
;   files_uninstall.nsh une ligne Delete par fichier installe
; Installation par utilisateur seulement (aucun droit d'administrateur) : le programme range ses reglages a cote de lui, dans un
; dossier qui doit rester inscriptible. Le programme lui-meme n'ecrit jamais dans le registre ; seul cet installeur le fait :
; HKCU\Software\ShaderToMP4 (dossier, version), la cle de desinstallation et, si on le demande, « Ouvrir avec » pour .glsl et .montage.
; Options : /S (silencieux), /D=dossier (dernier argument), /LANG=<identifiant de langue Windows>, /DESKTOP et /ASSOC (composants facultatifs) ; a la desinstallation
; /S et /DELETE_SETTINGS (supprime aussi les reglages).

Unicode True
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 64

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WordFunc.nsh"
!include "nsDialogs.nsh"
!include "Sections.nsh"

!define REGKEY "Software\${APP_NAME}"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
!define EXE "ShaderToMP4.exe"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
InstallDirRegKey HKCU "${REGKEY}" "InstallDir"
BrandingText "${APP_NAME} ${APP_VERSION}"

VIProductVersion "${APP_VER4}"
VIAddVersionKey /LANG=1033 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_NAME} setup"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (C) ${APP_COPYRIGHT_YEAR} ${APP_PUBLISHER}"

Var InstalledVer
Var FfmpegOurs
Var PurgeCheck
Var Purge

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "$(installer_finish_run)"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSEFILE}"
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
UninstPage custom un.PurgePage un.PurgeLeave
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "Italian"
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "Dutch"
!insertmacro MUI_LANGUAGE "Portuguese"
!insertmacro MUI_LANGUAGE "SimpChinese"

!include "${GEN}\strings.nsh"

!insertmacro GetParameters
!insertmacro GetOptions
!insertmacro un.GetParameters
!insertmacro un.GetOptions
!insertmacro VersionCompare

; ---------------------------------------------------------------------------------------------------------------------------------
; Controles communs
; ---------------------------------------------------------------------------------------------------------------------------------

; Le programme tourne-t-il ? (classe de sa fenetre principale) : proposer de le fermer, ou abandonner en mode silencieux
!macro CheckRunning un
  Function ${un}CheckRunning
    ${Do}
      FindWindow $0 "ShaderToMP4Main"
      ${If} $0 = 0
        ${Break}
      ${EndIf}
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(installer_running)" /SD IDCANCEL IDRETRY +2
      Abort
    ${Loop}
  FunctionEnd
!macroend
!insertmacro CheckRunning ""
!insertmacro CheckRunning "un."

; Dossier d'installation sous Program Files : refuse (le programme y ecrirait ses reglages sans en avoir le droit)
Function CheckDir
  ${If} $PROGRAMFILES64 != ""
    StrLen $1 $PROGRAMFILES64
    StrCpy $2 $INSTDIR $1
    ${If} $2 == $PROGRAMFILES64
      MessageBox MB_OK|MB_ICONSTOP "$(installer_progfiles)" /SD IDOK
      Abort
    ${EndIf}
  ${EndIf}
  StrLen $1 $PROGRAMFILES32
  StrCpy $2 $INSTDIR $1
  ${If} $2 == $PROGRAMFILES32
    MessageBox MB_OK|MB_ICONSTOP "$(installer_progfiles)" /SD IDOK
    Abort
  ${EndIf}
FunctionEnd

Function DirLeave
  Call CheckDir
FunctionEnd


; ---------------------------------------------------------------------------------------------------------------------------------
; Installation
; ---------------------------------------------------------------------------------------------------------------------------------

Section "$(installer_sec_program)" SecProgram
  SectionIn RO
  Call CheckDir
  SetOutPath "$INSTDIR"
  ; ffmpeg.exe : remplace seulement s'il a ete installe par cet installeur ; celui de l'utilisateur est toujours garde
  StrCpy $FfmpegOurs 1
  ${If} ${FileExists} "$INSTDIR\ffmpeg.exe"
    ReadRegDWORD $R0 HKCU "${REGKEY}" "FfmpegInstalled"
    ${If} $R0 <> 1
      StrCpy $FfmpegOurs 0
    ${EndIf}
  ${EndIf}
  ${If} $FfmpegOurs = 1
    File "${STAGE}\ffmpeg.exe"
  ${EndIf}
  !include "${GEN}\files_install.nsh"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${REGKEY}" "Version" "${APP_VERSION}"
  WriteRegDWORD HKCU "${REGKEY}" "FfmpegInstalled" $FfmpegOurs
  WriteRegStr HKCU "${UNKEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${UNKEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNKEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKCU "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNKEY}" "DisplayIcon" "$INSTDIR\${EXE},0"
  WriteRegStr HKCU "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr HKCU "${UNKEY}" "URLInfoAbout" "${APP_URL}"
  WriteRegDWORD HKCU "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNKEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNKEY}" "EstimatedSize" ${SIZE_KB}
SectionEnd

Section "$(installer_sec_startmenu)" SecStart
  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${EXE}"
SectionEnd

Section /o "$(installer_sec_desktop)" SecDesktop
  SetOutPath "$INSTDIR"
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${EXE}"
SectionEnd

; « Ouvrir avec » pour .glsl et .montage, pour cet utilisateur seulement ; le programme par defaut n'est pas change
Section /o "$(installer_sec_assoc)" SecAssoc
  WriteRegStr HKCU "Software\Classes\${APP_NAME}.File" "" "${APP_NAME}"
  WriteRegStr HKCU "Software\Classes\${APP_NAME}.File\DefaultIcon" "" "$INSTDIR\${EXE},0"
  WriteRegStr HKCU "Software\Classes\${APP_NAME}.File\shell\open\command" "" '"$INSTDIR\${EXE}" "%1"'
  WriteRegStr HKCU "Software\Classes\.glsl\OpenWithProgids" "${APP_NAME}.File" ""
  WriteRegStr HKCU "Software\Classes\.montage\OpenWithProgids" "${APP_NAME}.File" ""
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecProgram} "$(installer_desc_program)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStart} "$(installer_desc_startmenu)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "$(installer_desc_desktop)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} "$(installer_desc_assoc)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  ${GetParameters} $R0
  ${GetOptions} $R0 "/LANG=" $R1
  ${IfNot} ${Errors}
    StrCpy $LANGUAGE $R1
  ${EndIf}
  ClearErrors
  ; /DESKTOP et /ASSOC choisissent ces composants (utile en mode silencieux, ou ils ne sont pas coches par defaut)
  ${GetOptions} $R0 "/DESKTOP" $R1
  ${IfNot} ${Errors}
    !insertmacro SelectSection ${SecDesktop}
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/ASSOC" $R1
  ${IfNot} ${Errors}
    !insertmacro SelectSection ${SecAssoc}
  ${EndIf}
  ClearErrors
  Call CheckRunning
  ; une version plus recente deja installee n'est pas remplacee
  ReadRegStr $InstalledVer HKCU "${REGKEY}" "Version"
  ${If} $InstalledVer != ""
    ${VersionCompare} $InstalledVer "${APP_VERSION}" $R2
    ${If} $R2 = 1
      MessageBox MB_OK|MB_ICONSTOP "$(installer_older)" /SD IDOK
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd

; ---------------------------------------------------------------------------------------------------------------------------------
; Desinstallation
; ---------------------------------------------------------------------------------------------------------------------------------

Function un.onInit
  ${un.GetParameters} $R0
  ${un.GetOptions} $R0 "/LANG=" $R1
  ${IfNot} ${Errors}
    StrCpy $LANGUAGE $R1
  ${EndIf}
  ClearErrors
  ${un.GetOptions} $R0 "/DELETE_SETTINGS" $R1
  ${IfNot} ${Errors}
    StrCpy $Purge 1
  ${EndIf}
  ClearErrors
  Call un.CheckRunning
FunctionEnd

Function un.PurgePage
  !insertmacro MUI_HEADER_TEXT "$(installer_purge_title)" "$(installer_purge_sub)"
  nsDialogs::Create 1018
  Pop $0
  ${NSD_CreateCheckbox} 0 10u 100% 36u "$(installer_purge_check)"
  Pop $PurgeCheck
  ${If} $Purge = 1
    ${NSD_Check} $PurgeCheck
  ${EndIf}
  nsDialogs::Show
FunctionEnd

Function un.PurgeLeave
  ${NSD_GetState} $PurgeCheck $Purge
FunctionEnd

Section "Uninstall"
  !include "${GEN}\files_uninstall.nsh"
  ReadRegDWORD $0 HKCU "${REGKEY}" "FfmpegInstalled"
  ${If} $0 = 1
    Delete "$INSTDIR\ffmpeg.exe"
  ${EndIf}
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR\lang"
  ; reglages et fichiers du programme : gardes, sauf demande explicite ; les shaders et les videos ne sont jamais touches
  ${If} $Purge = 1
    Delete "$INSTDIR\config.ini"
    Delete "$INSTDIR\window.ini"
    Delete "$INSTDIR\queue.txt"
    RMDir /r "$INSTDIR\presets"
    RMDir /r "$INSTDIR\params"
    RMDir /r "$INSTDIR\jobs"
    RMDir /r "$INSTDIR\cache"
  ${EndIf}
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  DeleteRegValue HKCU "Software\Classes\.glsl\OpenWithProgids" "${APP_NAME}.File"
  DeleteRegValue HKCU "Software\Classes\.montage\OpenWithProgids" "${APP_NAME}.File"
  DeleteRegKey /ifempty HKCU "Software\Classes\.glsl\OpenWithProgids"
  DeleteRegKey /ifempty HKCU "Software\Classes\.montage\OpenWithProgids"
  DeleteRegKey /ifempty HKCU "Software\Classes\.glsl"
  DeleteRegKey /ifempty HKCU "Software\Classes\.montage"
  DeleteRegKey HKCU "Software\Classes\${APP_NAME}.File"
  DeleteRegKey HKCU "${UNKEY}"
  DeleteRegKey HKCU "${REGKEY}"
  RMDir "$INSTDIR"
SectionEnd
