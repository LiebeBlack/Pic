# Script de PowerShell para registrar la tarea de actualización programada
$ErrorActionPreference = 'SilentlyContinue'
$taskName = 'ARTPICST AutoUpdater'

$exePath = $args[0]
if (-not $exePath) {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\auto_updater.exe'),
        (Join-Path $PSScriptRoot 'auto_updater.exe'),
        (Join-Path $env:ProgramFiles 'ARTPICST\auto_updater.exe')
    )
    $exePath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $exePath -or -not (Test-Path $exePath)) {
    Write-Warning "[ARTPICST] No se encontro auto_updater.exe para programar la tarea."
    exit 1
}

$exists = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($exists) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
}

$action = New-ScheduledTaskAction -Execute $exePath -Argument '--silent'
$trigger = New-ScheduledTaskTrigger -Daily -At '09:00' -DaysInterval 2
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Force | Out-Null
Write-Host "[ARTPICST] Tarea programada registrada exitosamente: $taskName"
