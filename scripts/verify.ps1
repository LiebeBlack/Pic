# ============================================================================
# verify.ps1 - Punto único de entrada para la calidad de ARTPICST
# ----------------------------------------------------------------------------
# Reúne en un solo comando todos los gates del proyecto:
#
#   0. Gate de consistencia del baseline SIMD (fuente única, sin compilador)
#   1. Gate de política SIMD  (sin AVX + identidades enteras + máscaras PSHUFB)
#   2. Tests del núcleo de imagen    (LUT, allocator, SIMD alineado, transformadas)
#   3. Tests EXIF con offsets hostiles
#   4. Fuzzer determinista del parser JSON
#   5. Selftest del auto-actualizador (si el binario ya está compilado)
#   6. Banco de kernels (opcional, -Bench)
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File scripts\verify.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\verify.ps1 -Bench
#   powershell -ExecutionPolicy Bypass -File scripts\verify.ps1 -SkipBuild
#
# Exit: 0 = todo verde · 1 = algún gate ha fallado (se imprime cuál)
#
# IMPORTANTE: el baseline SIMD es SSE4.2 y se LEE de tools/build-baseline.json
# (única fuente de verdad). Los tests se compilan con ese flag a propósito: sin
# él el núcleo cae a la ruta SSE2 y los kernels SSE4.1 del visor quedarían sin
# cubrir, pero los tests seguirían pasando.
# ============================================================================

[CmdletBinding()]
param(
    [switch]$Bench,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$script:Failed = @()

function Step($name) {
    Write-Host ""
    Write-Host "=== $name ===" -ForegroundColor Cyan
}

function Info($message) {
    Write-Host $message -ForegroundColor DarkGray
}

function Fail($name, $message) {
    Write-Host "[FALLO] $name : $message" -ForegroundColor Red
    $script:Failed += "$name ($message)"
}

function Find-VisualStudio {
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    return $null
}

# ---------------------------------------------------------------------------
# Entorno de compilación
# ---------------------------------------------------------------------------
if (-not (Test-Path "build")) { New-Item -ItemType Directory -Path "build" -Force | Out-Null }

if (-not $SkipBuild) {
    $vs = Find-VisualStudio
    if (-not $vs) {
        Fail "Toolchain" "no se encontró Visual Studio (vcvars64.bat). Usa -SkipBuild para validar solo la política SIMD."
    } else {
        foreach ($line in (cmd /c "`"$vs`" && set")) {
            if ($line -match '^(.+)=(.*)$') {
                [Environment]::SetEnvironmentVariable($matches[1], $matches[2])
            }
        }
    }
}

# Baseline SIMD: leído de la fuente única (nunca escrito aquí).
$simdFlags = @()
foreach ($field in @("msvc_flag", "msvc_define")) {
    $value = (& powershell -NoProfile -ExecutionPolicy Bypass -File "tools\baseline.ps1" $field | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($value)) {
        Write-Host "[FALLO] Baseline : no se pudo leer '$field' de tools/build-baseline.json" -ForegroundColor Red
        exit 1
    }
    $simdFlags += $value
}
Info "Baseline SIMD (de tools/build-baseline.json): $($simdFlags -join ' ')"
$testCommon = @("/nologo", "/EHsc", "/std:c++20", "/O2", "/utf-8", "/W4", "/permissive-")

function Build-Test($out, $src, $includes, $libs) {
    if ($SkipBuild) { return (Test-Path $out) }
    # 'args' es una variable automática de PowerShell: se usa un nombre propio.
    $clArgs = @()
    $clArgs += $testCommon
    $clArgs += $simdFlags
    $clArgs += $includes
    $clArgs += "/Fe:`"$out`""
    $clArgs += $src
    if ($libs.Count -gt 0) { $clArgs += "/link"; $clArgs += $libs }
    & cl @clArgs
    return ($LASTEXITCODE -eq 0)
}

# ---------------------------------------------------------------------------
# 0. Consistencia del baseline SIMD (no necesita compilador ni Python)
# ---------------------------------------------------------------------------
Step "0. Consistencia del baseline SIMD (fuente única entre pipelines)"
& powershell -NoProfile -ExecutionPolicy Bypass -File "tests\check_build_baseline.ps1" -Quiet
if ($LASTEXITCODE -ne 0) { Fail "Baseline" "check_build_baseline.ps1 devolvió $LASTEXITCODE" }

# ---------------------------------------------------------------------------
# 1. Gate de política SIMD (no necesita compilador)
# ---------------------------------------------------------------------------
Step "1. Política SIMD (sin AVX, identidades enteras, máscaras PSHUFB)"
& python tests\check_simd_policy.py --root .
if ($LASTEXITCODE -ne 0) { Fail "Política SIMD" "check_simd_policy.py devolvió $LASTEXITCODE" }

# ---------------------------------------------------------------------------
# 2. Tests del núcleo de imagen
# ---------------------------------------------------------------------------
Step "2. Núcleo de imagen (incluye kernels SIMD alineados a 64 B)"
if (Build-Test "build\image_core_test.exe" "tests\image_core_test.cpp" @("/I.", "/Iinclude") @("kernel32.lib")) {
    & build\image_core_test.exe
    if ($LASTEXITCODE -ne 0) { Fail "image_core" "tests del núcleo con fallos" }
} else {
    Fail "image_core" "no compiló"
}

# ---------------------------------------------------------------------------
# 3. Tests EXIF
# ---------------------------------------------------------------------------
Step "3. Orientación EXIF (JPEGs sintéticos y offsets hostiles)"
if (Build-Test "build\jpeg_exif_test.exe" "tests\jpeg_exif_test.cpp" @("/I.") @("kernel32.lib", "shell32.lib")) {
    & build\jpeg_exif_test.exe
    if ($LASTEXITCODE -ne 0) { Fail "jpeg_exif" "tests EXIF con fallos" }
} else {
    Fail "jpeg_exif" "no compiló"
}

# ---------------------------------------------------------------------------
# 4. Fuzzer del parser JSON
# ---------------------------------------------------------------------------
Step "4. Fuzzer determinista del parser JSON"
if ($SkipBuild) {
    if (Test-Path "build\release_json_fuzzer.exe") {
        & build\release_json_fuzzer.exe
        if ($LASTEXITCODE -ne 0) { Fail "release_json" "fuzzer con fallos" }
    } else {
        Info "[omitido] build\release_json_fuzzer.exe no existe"
    }
} elseif (Build-Test "build\release_json_fuzzer.exe" "tests\release_json_fuzzer.cpp" @("/I.") @("kernel32.lib")) {
    & build\release_json_fuzzer.exe
    if ($LASTEXITCODE -ne 0) { Fail "release_json" "fuzzer con fallos" }
} else {
    Fail "release_json" "no compiló"
}

# ---------------------------------------------------------------------------
# 5. Selftest del actualizador (si ya está compilado)
# ---------------------------------------------------------------------------
Step "5. Selftest del auto-actualizador"
if (Test-Path "build\artpicst_updater.exe") {
    & build\artpicst_updater.exe --selftest
    if ($LASTEXITCODE -ne 0) { Fail "updater" "selftest con fallos" }
} else {
    Info "[omitido] build\artpicst_updater.exe no existe (ejecuta build.ps1 primero)"
}

# ---------------------------------------------------------------------------
# 6. Banco de kernels (informativo, nunca falla por sí mismo)
# ---------------------------------------------------------------------------
if ($Bench) {
    Step "6. Banco de kernels (baseline SSE4.2)"
    if (Build-Test "build\kernel_bench.exe" "tests\kernel_bench.cpp" @("/I.", "/Iinclude") @("kernel32.lib")) {
        & build\kernel_bench.exe
        Write-Host ""
        Write-Host "Compara estas cifras con las archivadas en docs\PERFORMANCE.md:" -ForegroundColor Gray
        Write-Host "para obtener la base SSE2, compila el mismo banco cambiando 'msvc_flag' en" -ForegroundColor Gray
        Write-Host "tools/build-baseline.json y quitando el define (es un edit temporal, no permanente)." -ForegroundColor Gray
    } else {
        Fail "kernel_bench" "no compiló"
    }
}

# ---------------------------------------------------------------------------
# Resumen
# ---------------------------------------------------------------------------
Write-Host ""
if ($script:Failed.Count -eq 0) {
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "VERIFY OK - todos los gates superados" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    exit 0
}

Write-Host "========================================" -ForegroundColor Red
Write-Host "VERIFY FALLIDO ($($script:Failed.Count) gate(s))" -ForegroundColor Red
Write-Host "========================================" -ForegroundColor Red
foreach ($f in $script:Failed) { Write-Host "  - $f" -ForegroundColor Red }
exit 1
