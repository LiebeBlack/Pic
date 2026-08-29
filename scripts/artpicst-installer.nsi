!ifndef APP_NAME
  !define APP_NAME "ARTPICST"
!endif
!ifndef APP_VERSION
  !define APP_VERSION "1.0.0"
!endif
!ifndef APP_PUBLISHER
  !define APP_PUBLISHER "ARTPICST"
!endif

!ifndef OutFile
  !define OutFile "artpicst-installer.exe"
!endif
!ifndef SourceDir
  !define SourceDir "..\dist"
!endif

Unicode true
Name "${APP_NAME}"
OutFile "${OutFile}"
InstallDir "$PROGRAMFILES64\ARTPICST"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Main"
    SetOutPath "$INSTDIR"
    File "${SourceDir}\artpicst.exe"
    File /nonfatal "${SourceDir}\README.md"

    CreateDirectory "$SMPROGRAMS\ARTPICST"
    CreateShortCut "$SMPROGRAMS\ARTPICST\ARTPICST.lnk" "$INSTDIR\artpicst.exe"
    CreateShortCut "$DESKTOP\ARTPICST.lnk" "$INSTDIR\artpicst.exe"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateShortCut "$SMPROGRAMS\ARTPICST\Uninstall ARTPICST.lnk" "$INSTDIR\Uninstall.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "DisplayIcon" "$INSTDIR\artpicst.exe"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "NoRepair" 1
SectionEnd

Section "Uninstall"
    Delete "$DESKTOP\ARTPICST.lnk"
    Delete "$SMPROGRAMS\ARTPICST\ARTPICST.lnk"
    Delete "$SMPROGRAMS\ARTPICST\Uninstall ARTPICST.lnk"
    RMDir "$SMPROGRAMS\ARTPICST"
    Delete "$INSTDIR\artpicst.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST"
SectionEnd
