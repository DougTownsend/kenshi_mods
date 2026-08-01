[CmdletBinding()]
param(
    [string]$StagingRoot = 'C:\Dev\show_effective_stats',
    [string]$SdkSetEnv = 'C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd'
)

$ErrorActionPreference = 'Stop'
$output = Join-Path $StagingRoot 'build\Release\x64'
$dll = Join-Path $output 'ShowEffectiveStats.dll'
$pdb = Join-Path $output 'ShowEffectiveStats.pdb'
foreach ($path in @($SdkSetEnv, $dll, $pdb)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required path was not found: $path" }
}

$command = 'call "{0}" /x64 /release >nul && dumpbin.exe /headers /exports "{1}"' -f $SdkSetEnv, $dll
$dumpbinText = (cmd.exe /d /s /c $command) -join "`n"
if ($LASTEXITCODE -ne 0) { throw "dumpbin failed with exit code $LASTEXITCODE" }
if ($dumpbinText -notmatch '8664 machine \(x64\)' -or $dumpbinText -notmatch 'startPlugin') {
    throw 'The DLL is not an x64 plugin exporting startPlugin.'
}
Get-FileHash -Algorithm SHA256 $dll, $pdb
Write-Host 'Verification succeeded.'
