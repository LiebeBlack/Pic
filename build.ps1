# ============================================================================
# build.ps1 - SISTEMA DE BUILD CANÓNICO de ARTPICST
# ----------------------------------------------------------------------------
# Este es el ÚNICO pipeline que define el orden de compilación del proyecto:
#   recursos -> visor -> updater -> selftest -> tests -> gate de política SIMD
#            -> payload -> instalador -> dist
#
# Los demás scripts NO duplican este pipeline: o delegan aquí (build.bat) o son
# variantes estrictamente del mismo orden para otro compilador (build_mingw.bat,
# Makefile, CMakeLists.txt). Si algo debe cambiar en el orden o en los flags, se
# cambia AQUÍ y en ningún otro sitio (ver BUILD.md, "Sistema de build").
#
# El BASELINE SIMD (SSE4.2, AVX/AVX2/AVX-512 prohibidos) NO se escribe en este
# archivo: se lee de tools/build-baseline.json, la única fuente de verdad, igual
# que hacen CMake, MinGW y los workflows. Un cambio de baseline es un solo edit.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File build.ps1
#   powershell -ExecutionPolicy Bypass -File build.ps1 -DryRun   # composición
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Bench    # + benchmark
#
#   -DryRun  imprime cada comando que se ejecutaría (con los flags reales ya
#            resueltos desde el JSON) y termina sin invocar al compilador. Sirve
#            para auditar el baseline sin toolchain y para depurar el pipeline.
# ============================================================================
[CmdletBinding()]
param(
    [switch]$DryRun,
    [switch]$Bench
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
if ($root) { Set-Location $root }

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ARTPICST - Build System (canónico)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
if ($DryRun) {
    Write-Host "-DryRun: se imprime la composición de cada paso; no se invoca al compilador." -ForegroundColor Yellow
}
Write-Host ""

function Fail($message) {
    Write-Host ""
    Write-Host $message -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# Herramientas: en -DryRun solo se imprime; en modo normal se ejecutan.
# ---------------------------------------------------------------------------
function Invoke-Tool {
    param([string]$Name, [string[]]$Arguments)
    Write-Host ("    {0} {1}" -f $Name, ($Arguments -join ' ')) -ForegroundColor DarkGray
    if ($DryRun) { return 0 }
    & $Name @Arguments
    return $LASTEXITCODE
}

function Invoke-Python {
    param([string[]]$Arguments)
    return (Invoke-Tool "python" $Arguments)
}

# ---------------------------------------------------------------------------
# [0] BASELINE SIMD - única fuente de verdad: tools/build-baseline.json
# ---------------------------------------------------------------------------
function Get-Baseline {
    $path = Join-Path $root "tools/build-baseline.json"
    if (-not (Test-Path $path)) {
        Fail "Error: falta tools/build-baseline.json (única fuente de verdad del baseline SIMD)."
    }
    try {
        $json = Get-Content -Path $path -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        Fail "Error: tools/build-baseline.json no es JSON válido: $($_.Exception.Message)"
    }
    $simd = $json.simd
    if (-not $simd -or -not $simd.msvc_flag -or -not $simd.gnu_flag -or -not $simd.define) {
        Fail "Error: tools/build-baseline.json no define simd.msvc_flag / simd.gnu_flag / simd.define."
    }
    # No se degrada en silencio: si alguien edita el baseline, se aborta.
    if ($simd.msvc_flag -notmatch 'SSE4\.2' -or $simd.define -ne "ARTPICST_TARGET_SSE42=1") {
        Fail ("Error: el baseline de tools/build-baseline.json no es SSE4.2/$($simd.define).`n" +
              "       El proyecto exige SSE4.2 (AVX/AVX2/AVX-512 prohibidos).")
    }
    return $simd
}

$simd = Get-Baseline
$SIMD_FLAG = $simd.msvc_flag
$SIMD_DEFINE = "/D$($simd.define)"
Write-Host "Baseline SIMD: $($simd.baseline)  ($SIMD_FLAG $SIMD_DEFINE)" -ForegroundColor Green
Write-Host "  origen: tools/build-baseline.json (única fuente de verdad)" -ForegroundColor DarkGray
Write-Host ""

# ---------------------------------------------------------------------------
# [0b] Gates ESTÁTICOS (no necesitan compilador): se ejecutan primero para que
#      una deriva del baseline o un AVX se detecten antes de compilar nada.
# ---------------------------------------------------------------------------
Write-Host "Verificando consistencia del baseline en todos los pipelines..." -ForegroundColor Gray
$gate = Join-Path $root $simd.consistency_gate
if (-not (Test-Path $gate)) {
    Fail "Error: falta el gate de consistencia del baseline ($($simd.consistency_gate))."
}
$gateExit = Invoke-Tool "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass",
                                       "-File", $simd.consistency_gate, "-Quiet")
if ($gateExit -ne 0) {
    Fail "El baseline SIMD está desincronizado entre pipelines (detalle arriba)."
}
Write-Host "Consistencia del baseline OK" -ForegroundColor Green
Write-Host ""

Write-Host "Verificando política SIMD (sin AVX)... " -NoNewline -ForegroundColor Gray
if ($DryRun) {
    Write-Host "(omitido en -DryRun: requiere Python)" -ForegroundColor DarkGray
} else {
    Write-Host ""
    $policyExit = Invoke-Python @($simd.policy_gate, "--root", ".")
    if ($policyExit -ne 0) {
        Fail "La política SIMD del proyecto no se cumple (ver detalle arriba)."
    }
    Write-Host "Política SIMD OK" -ForegroundColor Green
}
Write-Host ""

# ---------------------------------------------------------------------------
# Toolchain: Visual Studio
# ---------------------------------------------------------------------------
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
        if (Test-Path $path) { return $path }
    }
    return $null
}

$vsPath = Find-VisualStudio
if (-not $vsPath) {
    if ($DryRun) {
        Write-Host "Aviso: no se encontró Visual Studio; en -DryRun se continúa (solo se compone)." -ForegroundColor Yellow
    } else {
        Fail "Error: no se encontró Visual Studio. Usa build_mingw.bat (MinGW) o instala VS 2022 Build Tools."
    }
} else {
    Write-Host "Visual Studio encontrado: $vsPath" -ForegroundColor Green
    foreach ($line in (cmd /c "`"$vsPath`" && set")) {
        if ($line -match '^(.+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2])
        }
    }
    Write-Host ""
}

$directories = @("build", "dist", "installer\build", "updater\build")
if ($DryRun) {
    Write-Host "Directorios que se crearían: $($directories -join ', ')" -ForegroundColor DarkGray
} else {
    foreach ($dir in $directories) {
        if (-not (Test-Path $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
    }
}

# ---------------------------------------------------------------------------
# Sonda de toolchain: el compilador debe EMITIR SSE4.2 de verdad.
# ---------------------------------------------------------------------------
# Ojo: cl.exe emite la advertencia D9002 ("ignoring unknown option") y AUN ASÍ
# sale con código 0, así que la sonda inspecciona también el texto de salida.
# Un binario SSE2 pasaría los tests pero tendría otro rendimiento y otra
# cobertura: se aborta en lugar de degradar en silencio.
if ($DryRun) {
    Write-Host "Sonda de SSE4.2: cl /c /EHsc /std:c++latest $SIMD_FLAG $SIMD_DEFINE <probe> (omitida en -DryRun)" -ForegroundColor DarkGray
} else {
    Write-Host "Comprobando soporte de $SIMD_FLAG..." -ForegroundColor Gray
    $probeSrc = "build\simd_probe.cpp"
    $probeObj = "build\simd_probe.obj"
    $probeCode = @(
        "#include <smmintrin.h>",
        "int main() {",
        "    __m128i v = _mm_set1_epi8(1);",
        "    v = _mm_shuffle_epi8(v, v);",
        "    return _mm_testz_si128(v, v);",
        "}"
    ) -join "`r`n"
    Set-Content -Path $probeSrc -Value $probeCode -Encoding Ascii
    $probeOut = & cl /nologo /c /EHsc /std:c++latest $SIMD_FLAG $SIMD_DEFINE /Fo:$probeObj $probeSrc 2>&1
    $probeExit = $LASTEXITCODE
    $probeText = ($probeOut | Out-String)
    Remove-Item -Force -ErrorAction SilentlyContinue $probeSrc, $probeObj
    if ($probeExit -ne 0 -or $probeText -match 'D9002' -or $probeText -match 'unknown option') {
        Write-Host $probeText -ForegroundColor DarkGray
        Fail "Error: este cl.exe no soporta $SIMD_FLAG. Actualiza Visual Studio 2022 a 17.x o usa build_mingw.bat ($($simd.gnu_flag))."
    }
    Write-Host "$($simd.baseline) disponible" -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# [1/5] Recursos (visor y updater)
# ---------------------------------------------------------------------------
# Los recursos del INSTALADOR se compilan en [5/5], DESPUÉS de poblar
# resources/app/: su .rc incrusta el payload como RCDATA.
Write-Host "[1/5] Compilando recursos (visor y updater)..." -ForegroundColor Yellow

$rcExit = Invoke-Tool "rc" @("/nologo", "/fo", "build\artpicst.res", "artpicst.rc")
if ($rcExit -ne 0) { Fail "Error compilando los recursos del visor (artpicst.rc)" }

Push-Location updater
$updaterResExit = Invoke-Tool "rc" @("/nologo", "/fo", "..\build\artpicst_updater.res", "artpicst_updater.rc")
Pop-Location
if ($updaterResExit -ne 0) { Fail "Error compilando los recursos del updater (artpicst_updater.rc)" }

# ---------------------------------------------------------------------------
# [2/5] Visor
# ---------------------------------------------------------------------------
Write-Host "[2/5] Compilando visor (artpicst.exe)..." -ForegroundColor Yellow

$viewerArgs = @(
    "/nologo", "/EHsc", "/std:c++latest", "/O2", "/Ob3", "/Oi", "/GL", "/Gy", "/guard:cf",
    $SIMD_FLAG, $SIMD_DEFINE,
    "/utf-8", "/W4", "/permissive-", "/wd4324", "/I.", "/Iinclude",
    "/DUNICODE", "/D_UNICODE", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", "/DSTBI_WINDOWS_UTF8",
    "/DSTBIR_NO_AVX", "/DSTBIR_NO_AVX2", "/D_WIN32_WINNT=0x0601", "/D_CRT_SECURE_NO_WARNINGS",
    "/Fe:build\artpicst.exe", "src\main.cpp", "build\artpicst.res",
    "/link",
    "gdiplus.lib", "user32.lib", "kernel32.lib", "shell32.lib", "shlwapi.lib", "gdi32.lib",
    "msimg32.lib", "ole32.lib", "oleaut32.lib", "uuid.lib", "dwmapi.lib", "windowscodecs.lib",
    "comdlg32.lib", "d2d1.lib", "dwrite.lib", "advapi32.lib",
    "/MANIFEST:EMBED", "/MANIFESTINPUT:artpicst.manifest", "/SUBSYSTEM:WINDOWS",
    "/LTCG", "/OPT:REF", "/OPT:ICF",
    "/GUARD:CF", "/DYNAMICBASE", "/HIGHENTROPYVA", "/NXCOMPAT"
)
if ((Invoke-Tool "cl" $viewerArgs) -ne 0) { Fail "Error compilando el visor (artpicst.exe)" }

# ---------------------------------------------------------------------------
# [3/5] Updater
# ---------------------------------------------------------------------------
Write-Host "[3/5] Compilando updater (artpicst_updater.exe)..." -ForegroundColor Yellow

Push-Location updater
$updaterArgs = @(
    "/nologo", "/EHsc", "/std:c++latest", "/O2", "/Ob3", "/Oi", "/guard:cf",
    $SIMD_FLAG, $SIMD_DEFINE,
    "/utf-8", "/W4", "/permissive-", "/wd4324", "/I.", "/I..\installer",
    "/DUNICODE", "/D_UNICODE", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN",
    "/D_WIN32_WINNT=0x0601", "/D_CRT_SECURE_NO_WARNINGS",
    "/Fe:..\build\artpicst_updater.exe", "artpicst_updater.cpp", "..\build\artpicst_updater.res",
    "/link",
    "winhttp.lib", "gdiplus.lib", "shell32.lib", "shlwapi.lib", "user32.lib", "advapi32.lib",
    "gdi32.lib", "ole32.lib", "uuid.lib", "dwmapi.lib",
    "/SUBSYSTEM:WINDOWS", "/OPT:REF", "/OPT:ICF",
    "/GUARD:CF", "/DYNAMICBASE", "/HIGHENTROPYVA", "/NXCOMPAT"
)
$updaterExit = Invoke-Tool "cl" $updaterArgs
Pop-Location
if ($updaterExit -ne 0) { Fail "Error compilando el updater (artpicst_updater.exe)" }

# Gate de calidad: selftest del updater (parser JSON, SHA-256, comparador).
Write-Host "Ejecutando selftest del updater..." -ForegroundColor Gray
$selftestExit = Invoke-Tool "build\artpicst_updater.exe" @("--selftest")
if ($selftestExit -ne 0) { Fail "El selftest del updater ha fallado." }
Write-Host "Selftest del updater OK" -ForegroundColor Green

# ---------------------------------------------------------------------------
# [3b/5] Tests, fuzzing y banco de kernels
# ---------------------------------------------------------------------------
Write-Host "[3b/5] Compilando y ejecutando tests y fuzzing..." -ForegroundColor Yellow

$testCommon = @("/nologo", "/EHsc", "/std:c++20", "/O2", "/utf-8", "/W4", "/permissive-")

$coreTestArgs = $testCommon + @($SIMD_FLAG, $SIMD_DEFINE, "/I.", "/Iinclude",
                               "/Fe:build\image_core_test.exe", "tests\image_core_test.cpp",
                               "/link", "kernel32.lib")
if ((Invoke-Tool "cl" $coreTestArgs) -ne 0) { Fail "Error compilando tests\image_core_test.cpp" }
if ((Invoke-Tool "build\image_core_test.exe" @()) -ne 0) {
    Fail "Los tests del núcleo de imagen han fallado (incluye los kernels SIMD alineados)."
}

$benchArgs = $testCommon + @($SIMD_FLAG, $SIMD_DEFINE, "/I.", "/Iinclude",
                             "/Fe:build\kernel_bench.exe", "tests\kernel_bench.cpp",
                             "/link", "kernel32.lib")
if ((Invoke-Tool "cl" $benchArgs) -ne 0) { Fail "Error compilando tests\kernel_bench.cpp" }

$fuzzArgs = @("/nologo", "/EHsc", "/std:c++20", "/O2", "/utf-8", "/W4", "/permissive-", "/I.",
              "/Fe:build\release_json_fuzzer.exe", "tests\release_json_fuzzer.cpp",
              "/link", "kernel32.lib")
if ((Invoke-Tool "cl" $fuzzArgs) -ne 0) { Fail "Error compilando tests\release_json_fuzzer.cpp" }
if ((Invoke-Tool "build\release_json_fuzzer.exe" @()) -ne 0) { Fail "El fuzzer del parser JSON ha fallado." }

$exifArgs = @("/nologo", "/EHsc", "/std:c++20", "/O2", "/utf-8", "/W4", "/permissive-", "/I.",
              "/Fe:build\jpeg_exif_test.exe", "tests\jpeg_exif_test.cpp",
              "/link", "kernel32.lib", "shell32.lib")
if ((Invoke-Tool "cl" $exifArgs) -ne 0) { Fail "Error compilando tests\jpeg_exif_test.cpp" }
if ((Invoke-Tool "build\jpeg_exif_test.exe" @()) -ne 0) { Fail "Los tests EXIF han fallado." }
Write-Host "Tests y fuzzing OK" -ForegroundColor Green

if ($Bench) {
    Write-Host "Banco de kernels (baseline $($simd.baseline))..." -ForegroundColor Gray
    if ((Invoke-Tool "build\kernel_bench.exe" @()) -ne 0) { Fail "El banco de kernels ha fallado." }
    Write-Host "Copia las cifras en docs/PERFORMANCE.md (la tabla está sin rellenar a propósito)." -ForegroundColor Gray
} else {
    Write-Host "Banco de kernels compilado (build\kernel_bench.exe); usa -Bench para ejecutarlo." -ForegroundColor Gray
}

# ---------------------------------------------------------------------------
# [4/5] Payload autocontenido (resources/app/ para el instalador)
# ---------------------------------------------------------------------------
Write-Host "[4/5] Preparando payload autocontenido (resources/app/)..." -ForegroundColor Yellow
if ((Invoke-Python @("scripts\collect_app_payload.py", "--root", ".")) -ne 0) {
    Fail "Error preparando el payload autocontenido."
}

# ---------------------------------------------------------------------------
# [5/5] Instalador autocontenido
# ---------------------------------------------------------------------------
Write-Host "[5/5] Compilando instalador autocontenido (artpicst_installer.exe)..." -ForegroundColor Yellow

Push-Location installer
# Recursos DESPUÉS del payload: el .rc incrusta resources/app/ (payload).
$installerResExit = Invoke-Tool "rc" @("/nologo", "/fo", "..\build\artpicst_installer.res", "artpicst_installer.rc")
if ($installerResExit -ne 0) {
    Pop-Location
    Fail "Error compilando los recursos del instalador (artpicst_installer.rc)."
}
$installerArgs = @(
    "/nologo", "/EHsc", "/std:c++latest", "/O2", "/Ob3", "/Oi", "/guard:cf",
    $SIMD_FLAG, $SIMD_DEFINE,
    "/utf-8", "/W4", "/permissive-", "/wd4324", "/I.", "/I..\include",
    "/DUNICODE", "/D_UNICODE", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN",
    "/D_WIN32_WINNT=0x0601", "/D_CRT_SECURE_NO_WARNINGS",
    "/Fe:build\artpicst_installer.exe", "artpicst_installer.cpp", "..\build\artpicst_installer.res",
    "/link",
    "gdiplus.lib", "shlwapi.lib", "shell32.lib", "comctl32.lib", "dwmapi.lib", "user32.lib",
    "advapi32.lib", "gdi32.lib", "ole32.lib", "uuid.lib",
    "/SUBSYSTEM:WINDOWS", "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'",
    "/OPT:REF", "/OPT:ICF",
    "/GUARD:CF", "/DYNAMICBASE", "/HIGHENTROPYVA", "/NXCOMPAT"
)
$installerExit = Invoke-Tool "cl" $installerArgs
Pop-Location
if ($installerExit -ne 0) { Fail "Error compilando el instalador (artpicst_installer.exe)." }

# ---------------------------------------------------------------------------
# Distribución
# ---------------------------------------------------------------------------
Write-Host "Copiando archivos a dist..." -ForegroundColor Yellow

$artifacts = @(
    @("build\artpicst.exe", "dist\artpicst.exe"),
    @("build\artpicst_updater.exe", "dist\artpicst_updater.exe"),
    @("installer\build\artpicst_installer.exe", "dist\artpicst_installer.exe"),
    @("resources\artpicst.ico", "dist\artpicst.ico"),
    @("version.json", "dist\version.json"),
    @("README.md", "dist\README.md")
)
foreach ($pair in $artifacts) {
    if ($DryRun) {
        Write-Host ("    copy {0} -> {1}" -f $pair[0], $pair[1]) -ForegroundColor DarkGray
    } else {
        Copy-Item $pair[0] $pair[1] -Force
    }
}

Write-Host ""
if ($DryRun) {
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "DRY RUN COMPLETO (nada compilado)" -ForegroundColor Yellow
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Todos los comandos de arriba usan el baseline de tools/build-baseline.json." -ForegroundColor Gray
    exit 0
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "BUILD SUCCESSFUL" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Files in dist\:" -ForegroundColor White

foreach ($name in @("artpicst.exe", "artpicst_updater.exe", "artpicst_installer.exe")) {
    $p = "dist\$name"
    if (Test-Path $p) {
        $size = (Get-Item $p).Length
        $suffix = if ($name -eq "artpicst_installer.exe") { " (payload autocontenido)" } else { "" }
        Write-Host "$name`: $size bytes$suffix" -ForegroundColor Cyan
    }
}

Write-Host ""
Write-Host "Nota: el asset oficial de release es 'artpicst-installer.exe'." -ForegroundColor Gray
Write-Host "      (copia/renombra dist\artpicst_installer.exe al publicar)." -ForegroundColor Gray
Write-Host ""
Write-Host "Ready for GitHub release." -ForegroundColor Green
exit 0
