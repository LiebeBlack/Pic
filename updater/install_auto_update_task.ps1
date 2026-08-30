$ErrorActionPreference = 'Stop'
$taskName = 'ARTPICST AutoUpdater'
$exePath = $args[0]
if (-not $exePath) {
    $exePath = Join-Path $PSScriptRoot '..\auto_updater.exe'
}

if (-not (Test-Path $exePath)) {
    throw "No se encontro auto_updater.exe en: $exePath"
}

$exists = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($exists) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
}

$action = New-ScheduledTaskAction -Execute $exePath
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1)
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Force | Out-Null

# Ejecuta cada 2 dias en el horario indicado.
$task = Get-ScheduledTask -TaskName $taskName
$task.Triggers.Clear()
$task.Triggers.Add((New-ScheduledTaskTrigger -Daily -At '09:00' -DaysInterval 2))
$task | Set-ScheduledTask | Out-Null

Write-Host "Tarea creada: $taskName"
