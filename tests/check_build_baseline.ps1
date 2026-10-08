# ============================================================================
# tests/check_build_baseline.ps1 - GATE DE CONSISTENCIA DEL BASELINE SIMD
# ----------------------------------------------------------------------------
# El baseline SIMD del proyecto (SSE4.2, sin AVX) se fija en UN único archivo:
# tools/build-baseline.json. Este gate (sin compilador, sin Python) comprueba
# que eso sigue siendo cierto en la práctica y falla si vuelve la deriva:
#
#   1. el JSON existe y contiene el baseline correcto (SSE4.2 + su define);
#   2. cada consumidor declarado existe y REFERENCIA la fuente única;
#   3. ningún consumidor vuelve a ESCRIBIR el flag o el define (hardcodear);
#   4. build.bat delega en el pipeline canónico en lugar de duplicarlo;
#   5. no hay archivos de build huérfanos fuera del inventario del JSON.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tests\check_build_baseline.ps1
#   powershell -ExecutionPolicy Bypass -File tests\check_build_baseline.ps1 -Quiet
#
# Exit: 0 = todo consistente · 1 = deriva detectada (se imprime dónde)
# ============================================================================
[CmdletBinding()]
param(
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$script:Problems = @()

function Ok($message) {
    if (-not $Quiet) { Write-Host "  ok   $message" -ForegroundColor DarkGreen }
}

function Problem($message) {
    Write-Host "  FALLO $message" -ForegroundColor Red
    $script:Problems += $message
}

function Info($message) {
    if (-not $Quiet) { Write-Host "  ·    $message" -ForegroundColor DarkGray }
}

# ---------------------------------------------------------------------------
# 1. La fuente única
# ---------------------------------------------------------------------------
if (-not $Quiet) { Write-Host "1. Fuente única (tools/build-baseline.json)" -ForegroundColor Cyan }

$baselinePath = "tools/build-baseline.json"
if (-not (Test-Path $baselinePath)) {
    Problem "$baselinePath no existe: no hay fuente única del baseline SIMD."
    Write-Host ""
    Write-Host "GATE FALLIDO (1 problema)" -ForegroundColor Red
    exit 1
}

try {
    $json = Get-Content $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
} catch {
    Problem "$baselinePath no es JSON válido: $($_.Exception.Message)"
    exit 1
}

$simd = $json.simd
$values = @($simd.msvc_flag, $simd.gnu_flag, $simd.define)

if ($simd.msvc_flag -notmatch 'SSE4\.2') { Problem "simd.msvc_flag='$($simd.msvc_flag)' no es SSE4.2" } else { Ok "msvc_flag = $($simd.msvc_flag)" }
if ($simd.gnu_flag -notmatch 'msse4\.2') { Problem "simd.gnu_flag='$($simd.gnu_flag)' no es -msse4.2" } else { Ok "gnu_flag = $($simd.gnu_flag)" }
if ($simd.define -ne "ARTPICST_TARGET_SSE42=1") { Problem "simd.define='$($simd.define)' no es ARTPICST_TARGET_SSE42=1" } else { Ok "define = $($simd.define)" }
if (-not $simd.forbidden_tokens -or $simd.forbidden_tokens.Count -lt 4) {
    Problem "simd.forbidden_tokens está vacío o incompleto"
} else {
    Ok "forbidden = $($simd.forbidden_tokens -join ' ')"
}

# ---------------------------------------------------------------------------
# 2 y 3. Consumidores: existen, referencian la fuente y NO la duplican
# ---------------------------------------------------------------------------
if (-not $Quiet) {
    Write-Host ""
    Write-Host "2. Consumidores declarados en el JSON (referencian y no hardcodean)" -ForegroundColor Cyan
}

# Archivos que DEBEN leer el baseline y no pueden volver a escribirlo.
$consumers = @(
    "build.ps1",
    "CMakeLists.txt",
    "build.bat",
    "build_mingw.bat",
    "Makefile",
    "installer/build_installer.bat",
    "scripts/verify.ps1",
    ".github/workflows/build.yml",
    ".github/workflows/release.yml",
    ".github/workflows/nightly-fuzz.yml",
    "tests/check_simd_policy.py"
)

# El par que HACE CUMPLIR el baseline necesita conocer el valor esperado para
# poder validarlo: el lector y este propio gate son el ORÁCULO del JSON, no una
# segunda fuente (igual que check_simd_policy.py es el oráculo de las máscaras
# PSHUFB sin depender del compilador). Su conocimiento se limita a comprobar;
# si el JSON se degrada, los dos fallan.
$enforcers = @(
    "tools/baseline.ps1",
    "tests/check_build_baseline.ps1"
)

# Cómo se acepta que cada uno referencie la fuente única.
$referencePattern = 'build-baseline\.json|baseline\.ps1|baselinePath|simd\.msvc_flag|simd\.gnu_flag|simd_define|SIMD_FLAG|SIMD_DEFINE|SIMD_FLAGS'

# Quita comentarios para no acusar a la documentación dentro del código.
function Get-Code {
    param([string]$Path)
    $lines = Get-Content -Path $Path -Encoding UTF8
    foreach ($line in $lines) {
        $l = $line
        if ($l -match '^\s*(#|::|REM\s|<!--)') { continue }
        $l = $l -replace '\s+#\s.*$', ''      # comentario final (make, ps, yaml)
        $l = $l -replace '\s+::\s.*$', ''     # comentario final (batch)
        $l
    }
}

foreach ($file in ($consumers + $enforcers)) {
    if (-not (Test-Path $file)) {
        Problem "$file está declarado como consumidor pero no existe"
        continue
    }
    $code = Get-Code $file
    $joined = $code -join "`n"

    if ($joined -notmatch $referencePattern) {
        Problem "$file no referencia tools/build-baseline.json (o su lector tools/baseline.ps1)"
    } else {
        Ok "$file referencia la fuente única"
    }

    if ($enforcers -contains $file) {
        Ok "$file es oráculo del baseline (comprueba el valor; no lo fija)"
        continue
    }

    # Hardcodear el baseline es exactamente la deriva que este gate prohíbe.
    # El flag (/arch:SSE*, -msse*, -mavx*) nunca se escribe: se lee.
    $hardcoded = @()
    foreach ($needle in @('/arch:SSE', '-msse', '-mavx')) {
        if ($joined -match [regex]::Escape($needle)) { $hardcoded += $needle }
    }
    # El define SÍ puede aparecer para COMPROBARLO (una aserción del valor
    # esperado), pero no para fijarlo: definir el baseline es lo prohibido.
    foreach ($line in $code) {
        if ($line -match [regex]::Escape($simd.define)) {
            $isAssertion = $line -match '(?<![\w])-ne(?![\w])|(?<![\w])-eq(?![\w])|!=|==' 
            if (-not $isAssertion) { $hardcoded += $simd.define }
        }
    }
    if ($hardcoded.Count -gt 0) {
        Problem "$file vuelve a ESCRIBIR el baseline ($(($hardcoded | Select-Object -Unique) -join ', ')); debe leerlo del JSON"
    }
}

# ---------------------------------------------------------------------------
# 4. build.bat delega, no duplica el pipeline
# ---------------------------------------------------------------------------
if (-not $Quiet) {
    Write-Host ""
    Write-Host "4. build.bat delega en el pipeline canónico" -ForegroundColor Cyan
}

if (-not (Test-Path "build.bat")) {
    Problem "build.bat no existe"
} else {
    $bat = (Get-Code "build.bat") -join "`n"
    if ($bat -notmatch 'build\.ps1') {
        Problem "build.bat no invoca build.ps1 (debe delegar, no reimplementar el pipeline)"
    } else {
        Ok "build.bat invoca build.ps1"
    }
    if ($bat -match '(?m)^\s*cl\s' -or $bat -match '(?m)^\s*rc\s') {
        Problem "build.bat sigue compilando por su cuenta (cl/rc): el pipeline debe vivir solo en build.ps1"
    } else {
        Ok "build.bat no compila por su cuenta"
    }
}

# ---------------------------------------------------------------------------
# 5. Inventario: no puede haber pipelines huérfanos
# ---------------------------------------------------------------------------
if (-not $Quiet) {
    Write-Host ""
    Write-Host "5. Inventario de pipelines" -ForegroundColor Cyan
}

$declared = @($json.consumers.PSObject.Properties.Name)
$found = @()
foreach ($candidate in @("build.ps1", "build.bat", "build_mingw.bat", "Makefile", "CMakeLists.txt", "tools/baseline.ps1", "installer/build_installer.bat", "scripts/verify.ps1", "tests/check_build_baseline.ps1")) {
    if (Test-Path $candidate) { $found += $candidate }
}
if (Test-Path ".github/workflows") {
    $found += (Get-ChildItem ".github/workflows" -Filter *.yml | ForEach-Object { ".github/workflows/$($_.Name)" })
}
$found += "tests/check_simd_policy.py"

foreach ($f in $found) {
    if ($declared -notcontains $f) {
        Problem "$f no está declarado en el inventario 'consumers' de $baselinePath"
    }
}
foreach ($f in $declared) {
    if ($found -notcontains $f) {
        Info "$f figura en el inventario (no encontrado en disco; se ignora si es opcional)"
    }
}
if ($script:Problems.Count -eq 0) { Ok "inventario completo ($($found.Count) pipelines)" }

# ---------------------------------------------------------------------------
# Resumen
# ---------------------------------------------------------------------------
Write-Host ""
if ($script:Problems.Count -eq 0) {
    if (-not $Quiet) {
        Write-Host "========================================" -ForegroundColor Green
        Write-Host "BASELINE CONSISTENTE - fuente única respetada" -ForegroundColor Green
        Write-Host "========================================" -ForegroundColor Green
    } else {
        Write-Host "Baseline consistente (fuente única: $baselinePath)" -ForegroundColor Green
    }
    exit 0
}

Write-Host "========================================" -ForegroundColor Red
Write-Host "GATE FALLIDO ($($script:Problems.Count) problema(s))" -ForegroundColor Red
Write-Host "========================================" -ForegroundColor Red
foreach ($p in $script:Problems) { Write-Host "  - $p" -ForegroundColor Red }
Write-Host ""
Write-Host "Arreglo: el baseline SIMD se define SOLO en tools/build-baseline.json y los" -ForegroundColor Yellow
Write-Host "pipelines lo leen (tools/baseline.ps1, string(JSON) en CMake). Ver BUILD.md." -ForegroundColor Yellow
exit 1
