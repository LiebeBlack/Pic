# ============================================================================
# tools/baseline.ps1 - LECTOR del baseline SIMD (única fuente de verdad)
# ----------------------------------------------------------------------------
# El baseline SIMD del proyecto (SSE4.2, con AVX/AVX2/AVX-512 PROHIBIDOS) vive
# en UN solo archivo: tools/build-baseline.json. Este lector es la única forma
# de consumirlo desde scripts que no pueden parsear JSON por sí mismos (batch,
# make). CMake y PowerShell leen el JSON directamente; el resto llama aquí.
#
# Uso:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\baseline.ps1 msvc_flag
#     -> /arch:SSE4.2
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\baseline.ps1 gnu_flag
#     -> -msse4.2
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\baseline.ps1 define
#     -> ARTPICST_TARGET_SSE42=1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\baseline.ps1 msvc_define
#     -> /DARTPICST_TARGET_SSE42=1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\baseline.ps1 gnu_define
#     -> -DARTPICST_TARGET_SSE42=1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\baseline.ps1 msvc_all
#     -> /arch:SSE4.2 /DARTPICST_TARGET_SSE42=1     (una sola línea, para arrays)
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\baseline.ps1 gnu_all
#     -> -msse4.2 -DARTPICST_TARGET_SSE42=1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\baseline.ps1 baseline
#     -> SSE4.2
#
# AUTOVALIDACIÓN: el lector comprueba que los valores siguen siendo SSE4.2. Si
# alguien edita el JSON y degrada el baseline (por ejemplo a -msse2), TODOS los
# pipelines fallan aquí, con un mensaje accionable, en lugar de producir en
# silencio un binario con menos cobertura SIMD que la documentada.
# ----------------------------------------------------------------------------
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Field = "all"
)

$ErrorActionPreference = "Stop"

$baselinePath = Join-Path (Split-Path -Parent $PSScriptRoot) "tools/build-baseline.json"
if (-not (Test-Path $baselinePath)) {
    Write-Error "No se encontró $baselinePath (única fuente de verdad del baseline SIMD)."
    exit 1
}

try {
    $json = Get-Content -Path $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
} catch {
    Write-Error "tools/build-baseline.json no es JSON válido: $($_.Exception.Message)"
    exit 1
}

$simd = $json.simd
if (-not $simd) {
    Write-Error "tools/build-baseline.json no tiene el bloque 'simd'."
    exit 1
}

# ---------------------------------------------------------------------------
# Autovalidación del baseline (no se degrada en silencio)
# ---------------------------------------------------------------------------
$problems = @()
if (-not ($simd.msvc_flag  -match 'SSE4\.2'))    { $problems += "msvc_flag='$($simd.msvc_flag)' (se espera /arch:SSE4.2)" }
if (-not ($simd.gnu_flag   -match 'msse4\.2'))   { $problems += "gnu_flag='$($simd.gnu_flag)' (se espera -msse4.2)" }
if ($simd.define -ne "ARTPICST_TARGET_SSE42=1")  { $problems += "define='$($simd.define)' (se espera ARTPICST_TARGET_SSE42=1)" }
if ($problems.Count -gt 0) {
    Write-Host "El baseline SIMD de tools/build-baseline.json no es el del proyecto:" -ForegroundColor Red
    foreach ($p in $problems) { Write-Host "  - $p" -ForegroundColor Red }
    Write-Host "AVX/AVX2/AVX-512 están prohibidos y el baseline es SSE4.2." -ForegroundColor Red
    exit 1
}

$values = @{
    "baseline"    = $simd.baseline
    "msvc_flag"   = $simd.msvc_flag
    "gnu_flag"    = $simd.gnu_flag
    "define"      = $simd.define
    "macro"       = $simd.macro
    "msvc_define" = "/D$($simd.define)"
    "gnu_define"  = "-D$($simd.define)"
    "msvc_all"    = "$($simd.msvc_flag) /D$($simd.define)"
    "gnu_all"     = "$($simd.gnu_flag) -D$($simd.define)"
    "entrypoint"  = $json.canonical_entrypoint
    "policy_gate" = $simd.policy_gate
    "consistency_gate" = $simd.consistency_gate
    "forbidden"   = ($simd.forbidden_tokens -join " ")
}

if ($Field -eq "all") {
    foreach ($key in @("baseline", "msvc_flag", "gnu_flag", "define", "entrypoint",
                       "policy_gate", "consistency_gate", "forbidden")) {
        Write-Output "$key=$($values[$key])"
    }
    exit 0
}

if (-not $values.ContainsKey($Field)) {
    Write-Error "Campo desconocido '$Field'. Válidos: $((($values.Keys | Sort-Object) -join ', ')), all"
    exit 1
}

# Salida en una sola línea, sin adornos: es un valor para consumir, no un log.
Write-Output $values[$Field]
exit 0
