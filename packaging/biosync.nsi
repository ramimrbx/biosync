Unicode true
!include "MUI2.nsh"
!define APP "BioSync"
!define PUB "Right iTech"
!define VER "1.0.0"
!define RUNKEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\BioSync"

Name "BioSync"
OutFile "BioSync_Setup_v1.0.0.exe"
InstallDir "$PROGRAMFILES64\${PUB}\${APP}"
RequestExecutionLevel admin
ShowInstDetails show
BrandingText "BioSync ${VER} — Right iTech"
!define MUI_ICON "icon.ico"
!define MUI_UNICON "icon.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\BioSync.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; Silently remove a previous install so each install lands as a clean new version.
Function .onInit
  ReadRegStr $0 HKLM "${UNINSTKEY}" "UninstallString"
  StrCmp $0 "" done
  ExecWait '"$0" /S _?=$INSTDIR'
done:
FunctionEnd

Section "BioSync"
  SetOutPath "$INSTDIR"
  File /r "stage\*.*"

  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortcut "$SMPROGRAMS\${APP}\BioSync.lnk" "$INSTDIR\BioSync.exe"
  CreateShortcut "$DESKTOP\BioSync.lnk" "$INSTDIR\BioSync.exe"

  ; Start at login (all users) so the attendance server is always running.
  WriteRegStr HKLM "${RUNKEY}" "BioSync" '"$INSTDIR\BioSync.exe"'

  ; Pre-authorise in Windows Firewall so the ADMS listener never triggers the prompt (incl. after reboot).
  nsExec::Exec 'netsh advfirewall firewall delete rule name="BioSync"'
  nsExec::Exec 'netsh advfirewall firewall add rule name="BioSync" dir=in action=allow program="$INSTDIR\BioSync.exe" enable=yes profile=any'

  ; Add/Remove Programs entry.
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName"     "BioSync"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion"  "${VER}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher"       "${PUB}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon"     "$INSTDIR\BioSync.exe"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
  nsExec::Exec 'netsh advfirewall firewall delete rule name="BioSync"'
  DeleteRegValue HKLM "${RUNKEY}" "BioSync"
  DeleteRegKey HKLM "${UNINSTKEY}"
  Delete "$SMPROGRAMS\${APP}\BioSync.lnk"
  RMDir  "$SMPROGRAMS\${APP}"
  Delete "$DESKTOP\BioSync.lnk"
  RMDir /r "$INSTDIR"
SectionEnd
