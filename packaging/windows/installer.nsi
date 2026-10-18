; SPDX-License-Identifier: GPL-2.0-or-later
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "${GENERATED}\version.nsh"
Name "Convolver ${CONVOLVER_VERSION}"
OutFile "${OUTPUT}"
InstallDir "$PROGRAMFILES64\Convolver"
InstallDirRegKey HKLM "Software\Convolver" "InstallDir"
RequestExecutionLevel admin
Var PreviousInstallDir
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; Release builds sign the embedded uninstaller while NSIS creates it.
!ifdef SIGNING
!uninstfinalize 'signtool sign /sha1 "$%CONVOLVER_WINDOWS_CERT_SHA1%" /fd SHA256 /tr "$%CONVOLVER_TIMESTAMP_URL%" /td SHA256 "%1"'
!endif

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "This build requires x64 Windows."
    Abort
  ${EndIf}
  SetRegView 64
  ReadRegStr $PreviousInstallDir HKLM "Software\Convolver" "InstallDir"
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  ${If} $0 != 1
    MessageBox MB_ICONSTOP "Install the Microsoft Visual C++ v14 x64 Redistributable first. See the installation guide."
    Abort
  ${EndIf}
FunctionEnd

Section "Convolver"
  SetRegView 64
  StrCmp $PreviousInstallDir "" install
  IfFileExists "$PreviousInstallDir\Uninstall.exe" 0 install
  ExecWait '"$PreviousInstallDir\Uninstall.exe" /S _?=$PreviousInstallDir' $0
  ${If} $0 != 0
    Abort "Previous version could not be uninstalled. Close all plug-in hosts and retry."
  ${EndIf}
  ClearErrors
  Delete "$PreviousInstallDir\Uninstall.exe"
  ${If} ${Errors}
    Abort "Cannot remove the previous uninstaller. Close hosts and retry."
  ${EndIf}
  RMDir "$PreviousInstallDir"
  install:
  !include "${GENERATED}\install-files.nsh"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Convolver" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Convolver" "DisplayName" "Convolver"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Convolver" "DisplayVersion" "${CONVOLVER_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Convolver" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
  SetRegView 64
  !include "${GENERATED}\uninstall-files.nsh"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "Software\Convolver"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Convolver"
SectionEnd
