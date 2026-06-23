$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

$projectRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $projectRoot 'myShell.exe'
$inputFile = Join-Path $PSScriptRoot 'regression_input.txt'

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Build myShell.exe before running tests."
}

$ErrorActionPreference = 'Continue'

Push-Location $projectRoot
try {
    $output = Get-Content -LiteralPath $inputFile |
        & $exe 2>&1 |
        Out-String

    $checks = @(
        @{ Name = 'first PATH append'; Pattern = [regex]::Escape('C:\myShell-test-one') },
        @{ Name = 'second PATH append'; Pattern = [regex]::Escape('C:\myShell-test-two') },
        @{
            Name = 'Unicode working directory'
            Pattern = "t$([char]0x00E0)i li$([char]0x1EC7)u"
        },
        @{ Name = 'native dir'; Pattern = 'Directory of \.' },
        @{ Name = 'normal batch argument'; Pattern = 'ARG1=\[normal-value\]' },
        @{ Name = 'batch metacharacter rejection'; Pattern = 'unsafe character in batch' },
        @{ Name = 'clean exit'; Pattern = 'Goodbye!' }
    )

    foreach ($check in $checks) {
        if ($output -notmatch $check.Pattern) {
            throw "FAILED: $($check.Name)"
        }
    }

    if ($output -match 'ARG1=\[hello') {
        throw 'FAILED: unsafe batch argument was executed'
    }

    $longLine = 'x' * 1100
    $longOutput = @($longLine, 'date', 'exit') |
        & $exe 2>&1 |
        Out-String
    if ($longOutput -notmatch 'command too long' -or
        $longOutput -notmatch '\d{4}-\d{2}-\d{2}') {
        throw 'FAILED: overlong command recovery'
    }

    Write-Host 'All regression tests passed.'
}
finally {
    Pop-Location
}
