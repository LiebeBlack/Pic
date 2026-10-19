@echo off
:: ============================================================================
:: build.bat - DELEGADOR del pipeline canónico
:: ----------------------------------------------------------------------------
:: Este archivo NO compila y NO conoce ningún flag. El pipeline (orden de pasos,
:: flags de compilación, backend SIMD) vive SOLO en build.ps1, que a su vez lee
:: el baseline de tools/build-baseline.json.
::
:: Antes esta era una segunda implementación completa del pipeline: mismas
:: invocaciones de cl.exe y rc.exe con el baseline SIMD escrito otra vez. Cuatro
:: pipelines que hacían lo mismo y nueve sitios donde se fijaba el baseline son
:: la vía más rápida a que se desincronicen (y ya había pasado: kernel_bench se
:: compilaba aquí y no en build.ps1, y el flag /arch:SSE4.2 aparecía escrito a
:: mano en cada invocación). Ahora solo hay un pipeline.
::
:: Uso:  build.bat                 (equivale a build.ps1)
::       build.bat -DryRun         (imprime la composición, no compila)
::       build.bat -Bench          (además ejecuta el banco de kernels)
:: ============================================================================
setlocal EnableExtensions

cd /d "%~dp0"

if not exist tools\build-baseline.json (
    echo Error: no se encuentra tools\build-baseline.json ^(fuente unica del baseline^).
    exit /b 1
)

if not exist build.ps1 (
    echo Error: no se encuentra build.ps1 ^(pipeline canonico^) junto a build.bat.
    exit /b 1
)

echo ========================================
echo ARTPICST - Build System ^(delegando en build.ps1^)
echo ========================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo Compilacion FALLIDA ^(build.ps1 devolvio %RC%^).
    exit /b %RC%
)

exit /b 0
