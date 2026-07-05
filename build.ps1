param([switch]$PauseOnError)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

try {
    if (-not (Test-Path -LiteralPath 'src/vendor/sqlite/sqlite3.c') -or
        -not (Test-Path -LiteralPath 'src/vendor/sqlite/sqlite3.h')) {
        & "$PSScriptRoot\restore_sqlite.ps1"
        if ($LASTEXITCODE -ne 0) { throw 'SQLite source restoration failed.' }
    }

    $compiler = Get-Command g++ -ErrorAction SilentlyContinue
    $cCompiler = Get-Command gcc -ErrorAction SilentlyContinue
    if ($null -eq $compiler) {
        throw 'g++ was not found. Install MinGW-w64 or TDM-GCC and add it to PATH.'
    }
    if ($null -eq $cCompiler) { throw 'gcc was not found.' }

    & $cCompiler.Source -std=c11 -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION `
        -c src/vendor/sqlite/sqlite3.c -o sqlite3.o
    if ($LASTEXITCODE -ne 0) { throw "SQLite compilation failed with code $LASTEXITCODE." }

    & $compiler.Source -std=c++17 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ `
        src/main.cpp src/persistence.cpp sqlite3.o -o chat_server.exe -lws2_32 -lshell32 -lbcrypt
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed with code $LASTEXITCODE."
    }
    Remove-Item -LiteralPath sqlite3.o -Force -ErrorAction SilentlyContinue
    Write-Host 'Build complete: chat_server.exe' -ForegroundColor Green
}
catch {
    Remove-Item -LiteralPath sqlite3.o -Force -ErrorAction SilentlyContinue
    Write-Host "Build failed: $($_.Exception.Message)" -ForegroundColor Red
    if ($PauseOnError) { Read-Host 'Press Enter to close' }
    exit 1
}
