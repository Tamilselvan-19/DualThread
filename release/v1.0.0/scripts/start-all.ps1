[CmdletBinding()]
param(
    [string]$Root,
    [string]$KafkaHome = "C:\kafka",
    [switch]$SkipKafka
)

$ErrorActionPreference = "Stop"

# $PSScriptRoot is not reliable inside a param() default on Windows PowerShell 5.1,
# so derive the project root in the script body.
if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = if ($PSScriptRoot) {
        Split-Path -Parent $PSScriptRoot
    } else {
        (Get-Location).Path
    }
}

function Test-TcpPort {
    param(
        [string]$ComputerName = "127.0.0.1",
        [int]$Port
    )
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $result = $client.BeginConnect($ComputerName, $Port, $null, $null)
        if (-not $result.AsyncWaitHandle.WaitOne(400)) { return $false }
        $client.EndConnect($result)
        return $true
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

function Wait-TcpPort {
    param(
        [int]$Port,
        [int]$TimeoutSeconds = 30
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-TcpPort -Port $Port) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Start-AppService {
    param(
        [string]$Exe,
        [string]$Name,
        [string[]]$Arguments = @()
    )
    $stdout = Join-Path $script:Logs "$Name.out.log"
    $stderr = Join-Path $script:Logs "$Name.err.log"
    $params = @{
        FilePath               = $Exe
        WorkingDirectory       = $script:Root
        WindowStyle            = "Hidden"
        RedirectStandardOutput = $stdout
        RedirectStandardError  = $stderr
        PassThru               = $true
    }
    if ($Arguments.Count -gt 0) { $params.ArgumentList = $Arguments }
    $process = Start-Process @params
    Start-Sleep -Seconds 1
    if ($process.HasExited) {
        throw "$Name exited immediately (exit code $($process.ExitCode)). See $stderr"
    }
    Write-Host ("{0,-14} started (PID {1})" -f $Name, $process.Id)
    return $process
}

$script:Root = (Resolve-Path $Root).Path
$script:Logs = Join-Path $script:Root "logs"
$bin = Join-Path $script:Root "bin\Release"

if (-not (Test-Path (Join-Path $bin "ChatServer.exe"))) {
    throw "ChatServer.exe was not found in $bin. Build the solution first."
}
New-Item -ItemType Directory -Force -Path $script:Logs | Out-Null

# Do not start a second set of application services.
$appPorts = 5000, 6001, 6002
$busyPorts = @($appPorts | Where-Object { Test-TcpPort -Port $_ })
if ($busyPorts.Count -gt 0 -or (Get-Process -Name DBWriter -ErrorAction SilentlyContinue)) {
    $detail = if ($busyPorts.Count -gt 0) { "ports $($busyPorts -join ', ')" } else { "DBWriter" }
    Write-Host "Application services already appear to be running ($detail)." -ForegroundColor Yellow
    Write-Host "Run scripts\stop-all.ps1 first if you want a clean restart."
    exit 2
}

# MySQL and Memurai are Windows services in the verified environment. Start them
# only when they are stopped; this requires an elevated PowerShell session.
foreach ($serviceName in @("MySQL80", "Memurai")) {
    if (Test-TcpPort -Port $(if ($serviceName -eq "MySQL80") { 3306 } else { 6379 })) {
        continue
    }
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($service -and $service.Status -ne "Running") {
        Write-Host "Starting Windows service $serviceName..."
        try {
            Start-Service -Name $serviceName
        } catch {
            Write-Warning "Could not start $serviceName. Start it manually as Administrator."
        }
    }
}

if (-not (Wait-TcpPort -Port 3306 -TimeoutSeconds 15)) {
    Write-Warning "MySQL is not listening on 3306. The services will start, but database-backed operations may fail."
}
if (-not (Wait-TcpPort -Port 6379 -TimeoutSeconds 10)) {
    Write-Warning "Redis/Memurai is not listening on 6379. The servers will fall back to MySQL where possible."
}

if (-not $SkipKafka) {
    if (Test-TcpPort -Port 9092) {
        Write-Host "Kafka is already listening on 9092."
    } else {
        $kafkaBat = Join-Path $KafkaHome "bin\windows\kafka-server-start.bat"
        $kafkaProperties = Join-Path $KafkaHome "config\server.properties"
        if (-not (Test-Path $kafkaBat)) {
            throw "Kafka was not found at $KafkaBat. Pass -KafkaHome or use -SkipKafka."
        }
        if ($kafkaBat -match '\s' -or $kafkaProperties -match '\s') {
            throw "Kafka paths containing spaces are not supported by this launcher. Use a path without spaces or pass -SkipKafka and start Kafka manually."
        }
        Write-Host "Starting Kafka from $KafkaHome..."
        # The verified form is: cmd.exe /c "C:\kafka\bin\windows\kafka-server-start.bat C:\kafka\config\server.properties".
        # Passing it as one argument to Start-Process preserves that command line.
        $commandLine = "/c `"$kafkaBat $kafkaProperties`""
        Start-Process -FilePath "cmd.exe" -ArgumentList $commandLine `
            -WorkingDirectory $KafkaHome -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $script:Logs "kafka.out.log") `
            -RedirectStandardError (Join-Path $script:Logs "kafka.err.log") | Out-Null
        if (-not (Wait-TcpPort -Port 9092 -TimeoutSeconds 60)) {
            throw "Kafka did not start. Check $script:Logs\kafka.err.log and the Kafka installation."
        }
        Write-Host "Kafka is listening on 9092."
    }
} else {
    Write-Host "Skipping Kafka startup (-SkipKafka)."
}

$dbWriter = Start-AppService -Exe (Join-Path $bin "DBWriter.exe") -Name "DBWriter" `
    -Arguments @("config\dbwriter.ini")
$server1 = Start-AppService -Exe (Join-Path $bin "ChatServer.exe") -Name "ChatServer1" `
    -Arguments @("config\server1.ini")
$server2 = Start-AppService -Exe (Join-Path $bin "ChatServer.exe") -Name "ChatServer2" `
    -Arguments @("config\server2.ini")
$balancer = Start-AppService -Exe (Join-Path $bin "LoadBalancer.exe") -Name "LoadBalancer" `
    -Arguments @("config\loadbalancer.ini")

foreach ($port in @(6001, 6002, 5000)) {
    if (-not (Wait-TcpPort -Port $port -TimeoutSeconds 10)) {
        throw "Service did not open TCP port $port. Check the logs in $script:Logs."
    }
}

Write-Host ""
Write-Host "DualThread services are running:" -ForegroundColor Green
Write-Host ("  DBWriter      PID {0}" -f $dbWriter.Id)
Write-Host ("  ChatServer 1  PID {0}  TCP 6001 / UDP 7001" -f $server1.Id)
Write-Host ("  ChatServer 2  PID {0}  TCP 6002 / UDP 7002" -f $server2.Id)
Write-Host ("  LoadBalancer  PID {0}  TCP 5000 / UDP 5001" -f $balancer.Id)
Write-Host ""
Write-Host "Start clients with:"
Write-Host "  Start-Process `"$bin\ChatClient.exe`" -WorkingDirectory `"$script:Root`""
Write-Host ""
Write-Host "Logs: $script:Logs"
Write-Host "Stop: powershell -ExecutionPolicy Bypass -File scripts\stop-all.ps1"
exit 0
