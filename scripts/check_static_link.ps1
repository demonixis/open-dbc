param(
    [Parameter(Mandatory = $true)]
    [string]$Binary,
    [switch]$Strict
)

$Dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if (-not $Dumpbin) {
    throw "dumpbin.exe is required for the Windows static-link gate"
}

$BinaryPath = (Resolve-Path -LiteralPath $Binary -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
    throw "OpenDBC executable is not a regular file: $Binary"
}

$DumpOutput = @(& $Dumpbin.Source /dependents $BinaryPath 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin.exe failed to inspect $BinaryPath`: $($DumpOutput -join [Environment]::NewLine)"
}
$Dependencies = $DumpOutput |
    Where-Object { $_ -match '^\s+[A-Za-z0-9._-]+\.dll\s*$' } |
    ForEach-Object { $_.Trim() } |
    Sort-Object -Unique
if ($Dependencies.Count -eq 0) {
    throw "dumpbin.exe reported no dependencies for $BinaryPath"
}

$BinaryDirectory = Split-Path -Parent $BinaryPath
$AllowedSystemLibraries = @(
    "advapi32.dll",
    "bcrypt.dll",
    "cfgmgr32.dll",
    "comctl32.dll",
    "comdlg32.dll",
    "crypt32.dll",
    "dinput8.dll",
    "dwmapi.dll",
    "gdi32.dll",
    "imm32.dll",
    "kernel32.dll",
    "ole32.dll",
    "oleaut32.dll",
    "setupapi.dll",
    "shell32.dll",
    "user32.dll",
    "uxtheme.dll",
    "version.dll",
    "winmm.dll",
    "ws2_32.dll"
)
$Forbidden = @()
foreach ($Dependency in $Dependencies) {
    Write-Output $Dependency
    $Bundled = Join-Path $BinaryDirectory $Dependency
    if (Test-Path $Bundled) {
        $Forbidden += "$Dependency (bundled beside opendbc.exe)"
        continue
    }
    $ApiSet = $Dependency -like "api-ms-win-*.dll" -or $Dependency -like "ext-ms-win-*.dll"
    if (-not $ApiSet -and $Dependency.ToLowerInvariant() -notin $AllowedSystemLibraries) {
        $Forbidden += $Dependency
    }
}

if ($Forbidden.Count -gt 0) {
    Write-Error "Non-system dynamic linkage detected: $($Forbidden -join ', ')"
    if ($Strict) {
        exit 1
    }
}

Write-Output "OpenDBC Windows static-link gate passed."
