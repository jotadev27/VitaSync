; VitaSync -- Windows installer (NSIS)
;
; Packages the self-contained folder produced by make_portable_windows.sh
; into a normal Windows installer: installs to Program Files, adds a Start
; Menu entry and an uninstaller registered in Add/Remove Programs. Every
; file it installs already runs standalone (qt.conf + bundled DLLs), so
; this installer's only job is placement and a menu entry, not runtime setup.
;
; Usage (from the project root):
;   makensis -DSOURCE_DIR="installer/windows-portable" ^
;            -DOUT_FILE="installer/VitaSync-Setup.exe" ^
;            -DAPP_VERSION="1.0.0" ^
;            tools/windows_installer.nsi

Unicode true
Target amd64-unicode

!ifndef SOURCE_DIR
  !define SOURCE_DIR "installer/windows-portable"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "installer/VitaSync-Setup.exe"
!endif
!ifndef APP_VERSION
  !define APP_VERSION "1.0.0"
!endif

!define APP_NAME "VitaSync"
!define APP_PUBLISHER "jotadev27"
!define APP_EXE "vitasync.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\VitaSync"

!include "MUI2.nsh"

Name "${APP_NAME}"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\VitaSync"
InstallDirRegKey HKLM "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!define MUI_ICON "..\assets\logo\vitasync.ico"
!define MUI_UNICON "..\assets\logo\vitasync.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "FileDescription" "${APP_NAME} installer"
VIAddVersionKey "LegalCopyright" "${APP_PUBLISHER}"

Section "Install" SEC_INSTALL
    SetOutPath "$INSTDIR"
    File /r "${SOURCE_DIR}/*.*"

    CreateDirectory "$SMPROGRAMS\VitaSync"
    CreateShortcut "$SMPROGRAMS\VitaSync\VitaSync.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortcut "$DESKTOP\VitaSync.lnk" "$INSTDIR\${APP_EXE}"

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\VitaSync\VitaSync.lnk"
    RMDir "$SMPROGRAMS\VitaSync"
    Delete "$DESKTOP\VitaSync.lnk"

    RMDir /r "$INSTDIR"

    DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
