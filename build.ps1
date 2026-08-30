# ARTPICST Build System
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ARTPICST - Build System" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

function Find-VisualStudio {
    $vsPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    )
    
    foreach ($path in $vsPaths) {
        if (Test-Path $path) {
            return $path
        }
    }
    return $null
}

$vsPath = Find-VisualStudio
if (-not $vsPath) {
    Write-Host "Error: Visual Studio not found" -ForegroundColor Red
    exit 1
}

Write-Host "Visual Studio found: $vsPath" -ForegroundColor Green
Write-Host ""

$directories = @("build", "dist", "installer\build", "updater\build")
foreach ($dir in $directories) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
}

$envVars = cmd /c "`"$vsPath`" && set"
foreach ($line in $envVars) {
    if ($line -match '^(.+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2])
    }
}

Write-Host "[1/4] Building main program..." -ForegroundColor Yellow
$mainResult = & cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /I. /Iinclude /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSTBI_WINDOWS_UTF8 /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst.exe" src\main.cpp artpicst.manifest artpicst.rc /link gdiplus.lib user32.lib kernel32.lib shell32.lib shlwapi.lib gdi32.lib msimg32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowscodecs.lib comdlg32.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host "Error building main program" -ForegroundColor Red
    exit 1
}

Write-Host "[2/4] Building installer..." -ForegroundColor Yellow
Push-Location installer
$installerResult = & cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /I. /I..\include /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst_installer.exe" artpicst_installer.cpp /link gdiplus.lib shlwapi.lib shell32.lib comctl32.lib dwmapi.lib user32.lib advapi32.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF 2>&1
Pop-Location

if ($LASTEXITCODE -ne 0) {
    Write-Host "Error building installer" -ForegroundColor Red
    exit 1
}

Write-Host "[3/4] Building updater..." -ForegroundColor Yellow
Push-Location updater
$updaterResult = & cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fe:"build\auto_updater.exe" auto_updater_simple.cpp /link winhttp.lib shlwapi.lib shell32.lib  user32.lib advapi32.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF 2>&1
Pop-Location

if ($LASTEXITCODE -ne 0) {
    Write-Host "Error building updater" -ForegroundColor Red
    exit 1
}

Write-Host "[4/4] Copying files to dist..." -ForegroundColor Yellow
Copy-Item "build\artpicst.exe" "dist\artpicst.exe" -Force
Copy-Item "installer\build\artpicst_installer.exe" "dist\artpicst_installer.exe" -Force
Copy-Item "updater\build\auto_updater.exe" "dist\auto_updater.exe" -Force
Copy-Item "resources\artpicst.ico" "dist\artpicst.ico" -Force
Copy-Item "version.json" "dist\version.json" -Force

$batchContent = "@echo off`r`nsetlocal EnableExtensions`r`ncd /d `"%~dp0`"`r`nif exist `"auto_updater.exe`" (`r`n    start `"`" `"`auto_updater.exe`" --gui`r`n) else (`r`n    echo Updater not found`r`n    exit /b 1`r`n)"
Set-Content -Path "dist\auto_updater.bat" -Value $batchContent

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "BUILD SUCCESSFUL" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Files in dist\:" -ForegroundColor White

if (Test-Path "dist\artpicst.exe") {
    $size = (Get-Item "dist\artpicst.exe").Length
    Write-Host "artpicst.exe: $size bytes" -ForegroundColor Cyan
}
if (Test-Path "dist\artpicst_installer.exe") {
    $size = (Get-Item "dist\artpicst_installer.exe").Length
    Write-Host "artpicst_installer.exe: $size bytes" -ForegroundColor Cyan
}
if (Test-Path "dist\auto_updater.exe") {
    $size = (Get-Item "dist\auto_updater.exe").Length
    Write-Host "auto_updater.exe: $size bytes" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "Ready for GitHub release." -ForegroundColor Green
exit 0
