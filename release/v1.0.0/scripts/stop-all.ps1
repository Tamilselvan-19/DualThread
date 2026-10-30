[CmdletBinding()]
param(
    [switch]$StopKafka,
    [switch]$IncludeClients
)

$ErrorActionPreference = "Continue"

$names = @("LoadBalancer", "ChatServer", "DBWriter")
if ($IncludeClients) { $names += "ChatClient" }

foreach ($name in $names) {
    $processes = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
    if ($processes.Count -eq 0) {
        Write-Host ("{0,-14} not running" -f $name)
        continue
    }
    foreach ($process in $processes) {
        try {
            Stop-Process -Id $process.Id -Force -ErrorAction Stop
            Write-Host ("{0,-14} stopped (PID {1})" -f $name, $process.Id)
        } catch {
            Write-Warning "Could not stop $name PID $($process.Id): $($_.Exception.Message)"
        }
    }
}

if ($StopKafka) {
    $listener = Get-NetTCPConnection -State Listen -LocalPort 9092 -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($listener) {
        $ownerPid = $listener.OwningProcess
        try {
            Stop-Process -Id $ownerPid -Force -ErrorAction Stop
            Write-Host ("Kafka broker   stopped (PID {0})" -f $ownerPid)
        } catch {
            Write-Warning "Could not stop Kafka PID ${ownerPid}: $($_.Exception.Message)"
        }
    } else {
        Write-Host "Kafka broker   not listening on 9092"
    }
}

Start-Sleep -Milliseconds 500
$remaining = @()
foreach ($port in @(5000, 6001, 6002)) {
    if (Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue) {
        $remaining += $port
    }
}
if ($remaining.Count -eq 0) {
    Write-Host "DualThread application ports are free." -ForegroundColor Green
} else {
    Write-Warning "Still listening: $($remaining -join ', '). A different process may own the port."
}

Write-Host "MySQL and Memurai are left running. Use -StopKafka to stop the Kafka broker as well."
exit 0
