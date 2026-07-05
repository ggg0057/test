$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Restore-GzipParts {
    param([string]$Name)

    $vendor = Join-Path $PSScriptRoot 'src\vendor\sqlite'
    $target = Join-Path $vendor $Name
    if (Test-Path -LiteralPath $target) { return }

    $parts = @(Get-ChildItem -LiteralPath $vendor -Filter "$Name.gz.part*" -File | Sort-Object Name)
    if ($parts.Count -eq 0) { throw "Missing compressed SQLite source parts for $Name." }

    $compressed = Join-Path $env:TEMP ("chat_dictionary_" + $Name + '.gz')
    try {
        $output = [IO.File]::Create($compressed)
        try {
            foreach ($part in $parts) {
                $bytes = [IO.File]::ReadAllBytes($part.FullName)
                $output.Write($bytes, 0, $bytes.Length)
            }
        }
        finally { $output.Dispose() }

        $input = [IO.File]::OpenRead($compressed)
        $result = [IO.File]::Create($target)
        try {
            $gzip = [IO.Compression.GZipStream]::new($input, [IO.Compression.CompressionMode]::Decompress)
            try { $gzip.CopyTo($result) }
            finally { $gzip.Dispose() }
        }
        finally {
            $result.Dispose()
            $input.Dispose()
        }
    }
    finally { Remove-Item -LiteralPath $compressed -Force -ErrorAction SilentlyContinue }
}

Restore-GzipParts 'sqlite3.c'
Restore-GzipParts 'sqlite3.h'
Write-Host 'Embedded SQLite source restored.' -ForegroundColor Green
