!define APP_NAME "ARTPICST"
!define APP_VERSION "1.0.0"
!define APP_PUBLISHER "ARTPICST"

OutFile "${OutFile}"
InstallDir "$PROGRAMFILES64\ARTPICST"
RequestExecutionLevel admin

Page directory
Page instfiles

Section "Main"
    SetOutPath "$INSTDIR"
    File /r "${SourceDir}\*"

    CreateDirectory "$SMPROGRAMS\ARTPICST"
    CreateShortCut "$SMPROGRAMS\ARTPICST\ARTPICST.lnk" "$INSTDIR\artpicst.exe"
    CreateShortCut "$DESKTOP\ARTPICST.lnk" "$INSTDIR\artpicst.exe"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateShortCut "$SMPROGRAMS\ARTPICST\Uninstall ARTPICST.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
    Delete "$DESKTOP\ARTPICST.lnk"
    Delete "$SMPROGRAMS\ARTPICST\ARTPICST.lnk"
    Delete "$SMPROGRAMS\ARTPICST\Uninstall ARTPICST.lnk"
    RMDir "$SMPROGRAMS\ARTPICST"
    RMDir /r "$INSTDIR"
SectionEnd
