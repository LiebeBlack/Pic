!ifndef APP_NAME
  !define APP_NAME "ARTPICST"
!endif
!ifndef APP_VERSION
  !define APP_VERSION "1.1.0"
!endif
!ifndef APP_PUBLISHER
  !define APP_PUBLISHER "ARTPICST"
!endif
!ifndef APP_URL
  !define APP_URL "https://github.com/LiebeBlack/Pic"
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
SetCompress auto
XPStyle on

!define MUI_ICON "..\resources\artpicst.ico"
!define MUI_UNICON "..\resources\artpicst.ico"
!define MUI_WELCOMEPAGE_TITLE "Instalación de ${APP_NAME}"
!define MUI_WELCOMEPAGE_TEXT "Este asistente instalará ${APP_NAME} en su equipo y registrará las asociaciones de archivos de imagen."
!define MUI_FINISHPAGE_RUN "$INSTDIR\artpicst.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir ${APP_NAME} al finalizar"
!define MUI_FINISHPAGE_LINK "Visitar repositorio en GitHub"
!define MUI_FINISHPAGE_LINK_LOCATION "${APP_URL}"

!include "MUI2.nsh"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Spanish"

Function RegisterImageFileAssociations
    SetRegView 64

    WriteRegStr HKLM "Software\Classes\ARTPICST.Image" "" "ARTPICST Image"
    WriteRegStr HKLM "Software\Classes\ARTPICST.Image" "Content Type" "image/*"
    WriteRegStr HKLM "Software\Classes\ARTPICST.Image" "FriendlyTypeName" "ARTPICST Image"
    WriteRegStr HKLM "Software\Classes\ARTPICST.Image\DefaultIcon" "" "$INSTDIR\artpicst.exe,0"
    WriteRegStr HKLM "Software\Classes\ARTPICST.Image\shell\open\command" "" '"$INSTDIR\artpicst.exe" "%1"'
    WriteRegStr HKLM "Software\Classes\ARTPICST.Image\shell\open" "MuiVerb" "Abrir"
    WriteRegStr HKLM "Software\Classes\ARTPICST.Image\shell\open" "Icon" "$INSTDIR\artpicst.exe,0"

    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe" "FriendlyAppName" "ARTPICST"
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\shell\open\command" "" '"$INSTDIR\artpicst.exe" "%1"'
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\shell\open" "MuiVerb" "Abrir"
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\shell\open" "Icon" "$INSTDIR\artpicst.exe,0"

    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".png" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".jpg" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".jpeg" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".bmp" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".gif" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".tif" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".tiff" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".webp" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".ico" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".heic" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".heif" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".avif" ""
    WriteRegStr HKLM "Software\Classes\Applications\artpicst.exe\SupportedTypes" ".jfif" ""

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "DisplayIcon" "$INSTDIR\artpicst.exe,0"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "URLInfoAbout" "${APP_URL}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST" "NoRepair" 1
FunctionEnd

Section "Main"
    SetOutPath "$INSTDIR"
    File "${SourceDir}\artpicst.exe"
    File "${SourceDir}\artpicst.ico"
    File /nonfatal "${SourceDir}\README.md"
    File /nonfatal "${SourceDir}\auto_updater.exe"

    SetOutPath "$INSTDIR\updater"
    File "..\updater\install_auto_update_task.bat"
    File "..\updater\install_auto_update_task.ps1"

    CreateDirectory "$SMPROGRAMS\ARTPICST"
    CreateShortCut "$SMPROGRAMS\ARTPICST\ARTPICST.lnk" "$INSTDIR\artpicst.exe"
    CreateShortCut "$DESKTOP\ARTPICST.lnk" "$INSTDIR\artpicst.exe"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateShortCut "$SMPROGRAMS\ARTPICST\Uninstall ARTPICST.lnk" "$INSTDIR\Uninstall.exe"

    Call RegisterImageFileAssociations
    ExecWait 'cmd /c "$INSTDIR\updater\install_auto_update_task.bat" "$INSTDIR\auto_updater.exe"'
SectionEnd

Section "Uninstall"
    Delete "$DESKTOP\ARTPICST.lnk"
    Delete "$SMPROGRAMS\ARTPICST\ARTPICST.lnk"
    Delete "$SMPROGRAMS\ARTPICST\Uninstall ARTPICST.lnk"
    RMDir "$SMPROGRAMS\ARTPICST"

    Delete "$INSTDIR\artpicst.exe"
    Delete "$INSTDIR\artpicst.ico"
    Delete "$INSTDIR\auto_updater.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\updater\install_auto_update_task.bat"
    Delete "$INSTDIR\updater\install_auto_update_task.ps1"
    RMDir "$INSTDIR\updater"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    DeleteRegKey HKLM "Software\Classes\ARTPICST.Image"
    DeleteRegKey HKLM "Software\Classes\Applications\artpicst.exe"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST"
SectionEnd
