!if "${ARCH}" == "x64"
  Target amd64-unicode
!else
  Target x86-unicode
!endif
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 4

!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define APP_NAME "DiscCenter"
!define APP_ID "DiscCenter-${ARCH}"
!define PROGRAM_KEY "Software\${APP_ID}"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}"
!define PROG_ID "DiscCenter.${ARCH}.Image"

Name "${APP_NAME} ${VERSION} (${ARCH})"
Caption "${APP_NAME} ${VERSION} (${ARCH})"
OutFile "${OUT_EXE}"
InstallDir "$LOCALAPPDATA\Programs\${APP_ID}"
InstallDirRegKey HKCU "${PROGRAM_KEY}" "Path"
Icon "${ROOT}/cmake/disccenter.ico"
UninstallIcon "${ROOT}/cmake/disccenter.ico"
VIProductVersion "${PE_VERSION}"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "FileDescription" "${APP_NAME} Setup (${ARCH})"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026 DiscCenter contributors"
VIAddVersionKey "ProductVersion" "${VERSION}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${ROOT}/LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"

LangString MainSection ${LANG_ENGLISH} "DiscCenter"
LangString MainSection ${LANG_GERMAN} "DiscCenter"
LangString StartMenuSection ${LANG_ENGLISH} "Start menu shortcut"
LangString StartMenuSection ${LANG_GERMAN} "Verknüpfung im Startmenü"
LangString DesktopSection ${LANG_ENGLISH} "Desktop shortcut"
LangString DesktopSection ${LANG_GERMAN} "Desktop-Verknüpfung"
LangString IsoSection ${LANG_ENGLISH} "Offer DiscCenter in ISO Open with"
LangString IsoSection ${LANG_GERMAN} "DiscCenter für ISO-Dateien unter Öffnen mit anbieten"
LangString Needs64 ${LANG_ENGLISH} "This package requires 64-bit Windows. Use the x86 package for 32-bit Windows."
LangString Needs64 ${LANG_GERMAN} "Dieses Paket benötigt 64-Bit-Windows. Verwenden Sie das x86-Paket für 32-Bit-Windows."

Function .onInit
  SetShellVarContext current
  !if "${ARCH}" == "x64"
    ${IfNot} ${RunningX64}
      MessageBox MB_ICONSTOP "$(Needs64)" /SD IDOK
      SetErrorLevel 2
      Abort
    ${EndIf}
  !endif
FunctionEnd

Section "$(MainSection)" Core
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /oname=DiscCenter.exe "${APP_EXE}"
  File "${ROOT}/LICENSE"
  File "${ROOT}/README.md"
  File "${ROOT}/THIRD_PARTY_NOTICES.md"
  SetOutPath "$INSTDIR\licenses"
  File "${ROOT}/licenses/llvm-mingw.txt"
  File "${ROOT}/licenses/nsis.txt"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${PROGRAM_KEY}" "Path" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME} (${ARCH})"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\DiscCenter.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "$(StartMenuSection)"
  CreateShortCut "$SMPROGRAMS\${APP_NAME} (${ARCH}).lnk" "$INSTDIR\DiscCenter.exe"
SectionEnd

Section /o "$(DesktopSection)"
  CreateShortCut "$DESKTOP\${APP_NAME} (${ARCH}).lnk" "$INSTDIR\DiscCenter.exe"
SectionEnd

Section /o "$(IsoSection)"
  WriteRegStr HKCU "Software\Classes\${PROG_ID}" "" "${APP_NAME} ISO image (${ARCH})"
  WriteRegStr HKCU "Software\Classes\${PROG_ID}\DefaultIcon" "" "$INSTDIR\DiscCenter.exe,0"
  WriteRegStr HKCU "Software\Classes\${PROG_ID}\shell\open\command" "" '"$INSTDIR\DiscCenter.exe" "%1"'
  WriteRegStr HKCU "Software\Classes\.iso\OpenWithProgids" "${PROG_ID}" ""
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  Delete "$SMPROGRAMS\${APP_NAME} (${ARCH}).lnk"
  Delete "$DESKTOP\${APP_NAME} (${ARCH}).lnk"
  DeleteRegValue HKCU "Software\Classes\.iso\OpenWithProgids" "${PROG_ID}"
  DeleteRegKey HKCU "Software\Classes\${PROG_ID}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${PROGRAM_KEY}"
  Delete "$INSTDIR\DiscCenter.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.md"
  Delete "$INSTDIR\licenses\llvm-mingw.txt"
  Delete "$INSTDIR\licenses\nsis.txt"
  RMDir "$INSTDIR\licenses"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
