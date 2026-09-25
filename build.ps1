# ARTPICST Build System
# Orden de compilación: recursos -> visor -> updater -> payload -> instalador
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ARTPICST - Build System" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

function Find-VisualStudio {
    $vsPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )

    foreach ($path in $vsPaths) {
        if (Test-Path $path) {
            return $path
        }
    }
    return $null
}

function Fail($message) {
    Write-Host ""
    Write-Host $message -ForegroundColor Red
    exit 1
}

$vsPath = Find-VisualStudio
if (-not $vsPath) {
    Fail "Error: Visual Studio not found"
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

# ---------------------------------------------------------------------------
# [1/5] Recursos (visor, updater e instalador)
# ---------------------------------------------------------------------------
Write-Host "[1/5] Compilando recursos (visor y updater)..." -ForegroundColor Yellow

& rc /nologo /fo build\artpicst.res artpicst.rc
if ($LASTEXITCODE -ne 0) { Fail "Error compilando los recursos del visor (artpicst.rc)" }

Push-Location updater
& rc /nologo /fo ..\build\artpicst_updater.res artpicst_updater.rc
$updaterResExit = $LASTEXITCODE
Pop-Location
if ($updaterResExit -ne 0) { Fail "Error compilando los recursos del updater (artpicst_updater.rc)" }

# Nota: los recursos del INSTALADOR se compilan en el paso [5/5], DESPUÉS de
# poblar resources/app/ con el payload (el .rc los incrusta como RCDATA).

# ---------------------------------------------------------------------------
# [2/5] Visor
# ---------------------------------------------------------------------------
Write-Host "[2/5] Compilando visor (artpicst.exe)..." -ForegroundColor Yellow

& cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /GL /Gy /utf-8 /W4 /wd4324 /I. /Iinclude `
    /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSTBI_WINDOWS_UTF8 /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS `
    /Fe:"build\artpicst.exe" src\main.cpp build\artpicst.res `
    /link gdiplus.lib user32.lib kernel32.lib shell32.lib shlwapi.lib gdi32.lib msimg32.lib `
          ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowscodecs.lib comdlg32.lib d2d1.lib dwrite.lib `
          advapi32.lib `
    /MANIFEST:EMBED /MANIFESTINPUT:artpicst.manifest /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF
if ($LASTEXITCODE -ne 0) { Fail "Error compilando el visor (artpicst.exe)" }

# ---------------------------------------------------------------------------
# [3/5] Updater (artpicst_updater.exe)
# ---------------------------------------------------------------------------
Write-Host "[3/5] Compilando updater (artpicst_updater.exe)..." -ForegroundColor Yellow

Push-Location updater
& cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /utf-8 /W4 /wd4324 /I. /I..\installer `
    /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS `
    /Fe:"..\build\artpicst_updater.exe" artpicst_updater.cpp ..\build\artpicst_updater.res `
    /link winhttp.lib gdiplus.lib shell32.lib shlwapi.lib user32.lib advapi32.lib `
          gdi32.lib ole32.lib uuid.lib dwmapi.lib `
    /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF
$updaterExit = $LASTEXITCODE
Pop-Location
if ($updaterExit -ne 0) { Fail "Error compilando el updater (artpicst_updater.exe)" }

# Gate de calidad: selftest del updater (parser JSON, SHA-256, comparador de versiones).
Write-Host "Ejecutando selftest del updater..." -ForegroundColor Gray
& build\artpicst_updater.exe --selftest
if ($LASTEXITCODE -ne 0) { Fail "El selftest del updater ha fallado" }
Write-Host "Selftest del updater OK" -ForegroundColor Green

# ---------------------------------------------------------------------------
# [4/5] Payload autocontenido (resources/app/ para el instalador)
# ---------------------------------------------------------------------------
Write-Host "[4/5] Preparando payload autocontenido (resources/app/)..." -ForegroundColor Yellow

& python scripts\collect_app_payload.py --root .
if ($LASTEXITCODE -ne 0) { Fail "Error preparando el payload autocontenido" }

# ---------------------------------------------------------------------------
# [5/5] Instalador autocontenido (artpicst_installer.exe)
# ---------------------------------------------------------------------------
Write-Host "[5/5] Compilando instalador autocontenido (artpicst_installer.exe)..." -ForegroundColor Yellow

Push-Location installer
# Recursos DESPUÉS del payload: el .rc incrusta resources/app/ (payload)
& rc /nologo /fo ..\build\artpicst_installer.res artpicst_installer.rc
if ($LASTEXITCODE -ne 0) { Fail "Error compilando los recursos del instalador (artpicst_installer.rc)" }
& cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /utf-8 /W4 /wd4324 /I. /I..\include `
    /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS `
    /Fe:"build\artpicst_installer.exe" artpicst_installer.cpp ..\build\artpicst_installer.res `
    /link gdiplus.lib shlwapi.lib shell32.lib comctl32.lib dwmapi.lib user32.lib advapi32.lib `
          gdi32.lib ole32.lib uuid.lib `
    /SUBSYSTEM:WINDOWS "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'" /OPT:REF /OPT:ICF
$installerExit = $LASTEXITCODE
Pop-Location
if ($installerExit -ne 0) { Fail "Error compilando el instalador (artpicst_installer.exe)" }

# ---------------------------------------------------------------------------
# Distribución
# ---------------------------------------------------------------------------
Write-Host "Copiando archivos a dist..." -ForegroundColor Yellow
Copy-Item "build\artpicst.exe" "dist\artpicst.exe" -Force
Copy-Item "build\artpicst_updater.exe" "dist\artpicst_updater.exe" -Force
Copy-Item "installer\build\artpicst_installer.exe" "dist\artpicst_installer.exe" -Force
Copy-Item "resources\artpicst.ico" "dist\artpicst.ico" -Force
Copy-Item "version.json" "dist\version.json" -Force
Copy-Item "README.md" "dist\README.md" -Force

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
if (Test-Path "dist\artpicst_updater.exe") {
    $size = (Get-Item "dist\artpicst_updater.exe").Length
    Write-Host "artpicst_updater.exe: $size bytes" -ForegroundColor Cyan
}
if (Test-Path "dist\artpicst_installer.exe") {
    $size = (Get-Item "dist\artpicst_installer.exe").Length
    Write-Host "artpicst_installer.exe: $size bytes (payload autocontenido)" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "Nota: el asset oficial de release es 'artpicst-installer.exe'." -ForegroundColor Gray
Write-Host "      (copia/renombra dist\artpicst_installer.exe al publicar)." -ForegroundColor Gray
Write-Host ""
Write-Host "Ready for GitHub release." -ForegroundColor Green
exit 0
