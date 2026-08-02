[CmdletBinding()]
param(
    [string]$SharedRoot = 'Z:\xp_bars',
    [string]$StagingRoot = 'C:\Dev\xp_bars',
    [string]$SdkSetEnv = 'C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd',
    [string]$WindowsSdkInclude = 'C:\Program Files\Microsoft SDKs\Windows\v7.1\Include',
    [string]$WindowsSdkLib = 'C:\Program Files\Microsoft SDKs\Windows\v7.1\Lib\x64'
)

$ErrorActionPreference = 'Stop'
foreach ($path in @($SharedRoot, $SdkSetEnv, $WindowsSdkInclude, $WindowsSdkLib)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required path was not found: $path" }
}
foreach ($variable in @('KENSHILIB_DIR', 'KENSHILIB_DEPS_DIR', 'BOOST_INCLUDE_PATH', 'BOOST_ROOT')) {
    if (-not [Environment]::GetEnvironmentVariable($variable, 'Process')) {
        $userValue = [Environment]::GetEnvironmentVariable($variable, 'User')
        if (-not $userValue) { throw "$variable is not set." }
        [Environment]::SetEnvironmentVariable($variable, $userValue, 'Process')
    }
}
$env:XPBARS_WINDOWS_SDK_INCLUDE = $WindowsSdkInclude
$env:XPBARS_WINDOWS_SDK_LIB = $WindowsSdkLib
& robocopy $SharedRoot $StagingRoot /E /XD build artifacts .git /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) { throw "Staging copy failed with robocopy exit code $LASTEXITCODE" }
$project = Join-Path $StagingRoot 'project\XpBars.vcxproj'
$command = 'call "{0}" /x64 /release >nul && msbuild "{1}" /m /t:Build /p:Configuration=Release /p:Platform=x64' -f $SdkSetEnv, $project
cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
$output = Join-Path $StagingRoot 'build\Release\x64'
$dll = Join-Path $output 'XpBars.dll'; $pdb = Join-Path $output 'XpBars.pdb'
foreach ($path in @($dll, $pdb)) { if (-not (Test-Path -LiteralPath $path)) { throw "Build output was not found: $path" } }
$dumpbinCommand = 'call "{0}" /x64 /release >nul && dumpbin.exe /headers /exports "{1}"' -f $SdkSetEnv, $dll
$dumpbinText = (cmd.exe /d /s /c $dumpbinCommand) -join "`n"
if ($LASTEXITCODE -ne 0 -or $dumpbinText -notmatch '8664 machine \(x64\)' -or $dumpbinText -notmatch 'startPlugin') { throw 'The DLL is not an x64 plugin exporting startPlugin.' }
$sharedOutput = Join-Path $SharedRoot 'build\Release\x64'
New-Item -ItemType Directory -Force -Path $sharedOutput | Out-Null
Copy-Item -LiteralPath $dll, $pdb -Destination $sharedOutput -Force
Get-FileHash -Algorithm SHA256 $dll, $pdb
Write-Host "Build and verification succeeded: $sharedOutput"
