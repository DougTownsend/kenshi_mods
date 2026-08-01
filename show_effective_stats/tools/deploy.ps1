[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$KenshiRoot,
    [string]$StagingRoot = 'C:\Dev\show_effective_stats',
    [string]$SymbolsRoot = 'C:\Dev\symbols',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$sourceDir = Join-Path $StagingRoot 'build\Release\x64'
$dll = Join-Path $sourceDir 'ShowEffectiveStats.dll'
$pdb = Join-Path $sourceDir 'ShowEffectiveStats.pdb'
$templateDir = Join-Path $StagingRoot 'mod\ShowEffectiveStats'
$destination = Join-Path $KenshiRoot 'mods\ShowEffectiveStats'
foreach ($path in @((Join-Path $KenshiRoot 'kenshi_x64.exe'), $dll, $pdb, $templateDir)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required path was not found: $path" }
}
if (Get-Process -Name 'kenshi_x64' -ErrorAction SilentlyContinue) { throw 'Exit Kenshi before deploying the plugin.' }
if ((Test-Path -LiteralPath $destination) -and -not $Force) {
    throw "Refusing to overwrite existing mod directory: $destination. Re-run with -Force only if it is this mod."
}

New-Item -ItemType Directory -Force -Path $destination, $SymbolsRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $templateDir 'ShowEffectiveStats.mod') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $templateDir 'RE_Kenshi.json') -Destination $destination -Force
Copy-Item -LiteralPath $dll -Destination $destination -Force
Copy-Item -LiteralPath $pdb -Destination (Join-Path $SymbolsRoot 'ShowEffectiveStats.pdb') -Force
Write-Host "Deployed ShowEffectiveStats to $destination"
