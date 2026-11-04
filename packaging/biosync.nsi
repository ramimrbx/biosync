Unicode true
!include "MUI2.nsh"
; Metadata comes from appinfo.cmake via the CMake-generated appinfo.nsh — edit appinfo.cmake, not here.
!include "appinfo.nsh"
!define APP "${APP_NAME}"
!define PUB "${APP_PUBLISHER}"
!define VER "${APP_VERSION}"
!define RUNKEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\BioSync"
; Uninstall key of the Inno Setup build (AppId in installer/windows/BioSync.iss + "_is1").
!define INNO_UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\{7A880B11-3DDF-4EEC-997E-13591EA773AB}_is1"

Name "${APP}"
OutFile "BioSync_Setup_v${VER}.exe"
InstallDir "$PROGRAMFILES64\${PUB}\${APP}"
RequestExecutionLevel admin
ShowInstDetails show
BrandingText "${APP} ${VER} — ${PUB}"
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
  ; Close every running BioSync (background service + any open window) so its files can be
  ; replaced — otherwise the upgrade fails with "error opening file for writing".
  nsExec::Exec 'schtasks /End /TN "BioSync"'
  nsExec::Exec 'taskkill /F /IM BioSync.exe'
  Sleep 1000

  ; A version installed by the Inno Setup installer (installer/windows/BioSync.iss) registers under
  ; its AppId, in the 64-bit registry view. Remove it silently first, so the two installers never
  ; leave two copies / two "Apps & features" entries. Config in ProgramData\BioSync is kept.
  SetRegView 64
  ReadRegStr $1 HKLM "${INNO_UNINSTKEY}" "UninstallString"
  SetRegView 32
  StrCmp $1 "" noinno
  ExecWait '$1 /VERYSILENT /SUPPRESSMSGBOXES /NORESTART'
  Sleep 2000
noinno:

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

  ; Shared config dir (same one the app's AppPaths uses): both the SYSTEM boot service and the
  ; user's GUI read/write it. Grant Users modify so the GUI can save config the service will read.
  CreateDirectory "$COMMONPROGRAMDATA\BioSync"
  nsExec::Exec 'icacls "$COMMONPROGRAMDATA\BioSync" /grant *S-1-5-32-545:(OI)(CI)M /T'

  ; Run HEADLESS at system startup, as SYSTEM — no login required, no GUI. This replaces the old
  ; login Run key (remove any leftover from a previous version).
  DeleteRegValue HKLM "${RUNKEY}" "BioSync"
  nsExec::Exec 'schtasks /Create /TN "BioSync" /TR "\"$INSTDIR\BioSync.exe\" --headless" /SC ONSTART /RU SYSTEM /RL HIGHEST /F'
  ; schtasks gives every task a 72-hour run limit and "don't start / stop on battery" — that kills
  ; the service after 3 days of uptime (or on a laptop). Lift those limits and restart it if it ever
  ; fails. (Needs Windows 8+; on older Windows the task keeps the defaults.)
  nsExec::Exec `powershell -NoProfile -ExecutionPolicy Bypass -Command "$$s = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) -MultipleInstances IgnoreNew; Set-ScheduledTask -TaskName 'BioSync' -Settings $$s"`
  ; Start it now too, so it works without a reboot.
  nsExec::Exec 'schtasks /Run /TN "BioSync"'

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
  ; Stop + remove the headless boot task, and close any open window.
  nsExec::Exec 'schtasks /End /TN "BioSync"'
  nsExec::Exec 'taskkill /F /IM BioSync.exe'
  nsExec::Exec 'schtasks /Delete /TN "BioSync" /F'
  nsExec::Exec 'netsh advfirewall firewall delete rule name="BioSync"'
  DeleteRegValue HKLM "${RUNKEY}" "BioSync"
  DeleteRegKey HKLM "${UNINSTKEY}"
  Delete "$SMPROGRAMS\${APP}\BioSync.lnk"
  RMDir  "$SMPROGRAMS\${APP}"
  Delete "$DESKTOP\BioSync.lnk"
  RMDir /r "$INSTDIR"
SectionEnd
