param([switch]$NoOpen, [int]$Port = 0, [string]$AdminPassword = 'gensokyo-admin', [string]$ListenHost = '127.0.0.1', [switch]$OpenWebsite)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Test-PortAvailable([int]$CandidatePort) {
    $listener = $null
    try {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $CandidatePort)
        $listener.Start()
        return $true
    }
    catch {
        return $false
    }
    finally {
        if ($null -ne $listener) {
            $listener.Stop()
        }
    }
}

try {
    $server = Join-Path $PSScriptRoot 'chat_server.exe'
    if (-not (Test-Path -LiteralPath $server)) {
        & (Join-Path $PSScriptRoot 'build.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'The C++ server could not be built.' }
    }
    if ($Port -eq 0) {
        $Port = 8000..8010 | Where-Object { Test-PortAvailable $_ } | Select-Object -First 1
        if ($null -eq $Port) {
            throw 'Ports 8000-8010 are busy. Close the application using them and try again.'
        }
    }

    $arguments = @('--host', $ListenHost, '--port', [string]$Port, '--admin-password', $AdminPassword)
    if (-not $NoOpen) {
        $arguments += $(if ($OpenWebsite) { '--open-website' } else { '--open' })
    }

    Write-Host ''
    Write-Host 'C++ chat room is starting...' -ForegroundColor Green
    Write-Host "Mini app: http://127.0.0.1:$Port/"
    Write-Host "Website: http://127.0.0.1:$Port/web"
    if ($ListenHost -eq '0.0.0.0') {
        $localIp = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
            Select-Object -ExpandProperty IPAddress -First 1
        if ($localIp) {
            Write-Host "LAN website: http://${localIp}:$Port/web" -ForegroundColor Cyan
            Write-Host 'Other phones/computers on the same Wi-Fi can open this address.'
        }
    }
    Write-Host 'Keep this server window open. Press Ctrl+C to stop.'
    Write-Host ''
    & $server @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "C++ server exited unexpectedly with code $LASTEXITCODE."
    }
}
catch {
    Write-Host ''
    Write-Host "Startup failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Application folder: $PSScriptRoot"
    Read-Host 'Press Enter to close this window'
    exit 1
}
