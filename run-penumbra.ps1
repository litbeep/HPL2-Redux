<#
.SYNOPSIS
Launch the locally built Penumbra level viewer.
.EXAMPLE
.\run-penumbra.ps1
.EXAMPLE
.\run-penumbra.ps1 -GameDir 'D:\Games\Penumbra Overture' -Renderer raytraced
.EXAMPLE
.\run-penumbra.ps1 -FreeCamera -Frames 24
#>
[CmdletBinding()]
param(
    [string]$GameDir = 'C:\Steam\steamapps\common\Penumbra Overture',
    [string]$Map = 'level00_01_boat_cabin.dae',
    [string]$Start = 'link1',
    [ValidateSet('standard', 'raytraced')]
    [string]$Renderer = 'standard',
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$FreeCamera,
    [switch]$DebugAreas,
    [switch]$ScriptCheck,
    [ValidateRange(1, 2147483647)]
    [int]$ScriptSteps = 120,
    [ValidateRange(1, 2147483647)]
    [int]$Frames,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Host @'
Usage: .\run-penumbra.ps1 [options]

Defaults: Release build, Overture boat cabin, walking, standard renderer.
  -GameDir <path>       Your Penumbra install (or its redist directory)
  -Map <file>           Map filename (default: level00_01_boat_cabin.dae)
  -Start <name>         Entrance (default: link1; must exist in the map)
  -Renderer <name>      standard or raytraced
  -Configuration <name> Release or Debug
  -FreeCamera          Fly instead of walking; Q/E move down/up
  -DebugAreas          Show area bounds and labels; F3 toggles
  -ScriptCheck         Run scripts without opening windows
  -ScriptSteps <count> Number of 60 Hz ticks for ScriptCheck
  -Frames <count>      Render a fixed number of frames, then exit
  -Help                Show this help

Controls: WASD, mouse look, Shift faster, Space jumps or dismisses messages,
          left mouse interacts/picks up, right mouse examines, U uses a carried item,
          F3 toggles areas, Escape exits.
'@
    return
}

$executable = Join-Path $PSScriptRoot "build-premake\penumbra\$Configuration\Penumbra.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Penumbra executable not found: $executable. Build the penumbra product first (see BUILD.md)."
}

$installPath = (Resolve-Path -LiteralPath $GameDir).Path
$resourceConfig = Join-Path $installPath 'resources.cfg'
$redistConfig = Join-Path $installPath 'redist\resources.cfg'
if (-not ((Test-Path -LiteralPath $resourceConfig -PathType Leaf) -or
          (Test-Path -LiteralPath $redistConfig -PathType Leaf))) {
    throw "No resources.cfg found in '$installPath' or its redist directory. Set -GameDir to your Penumbra install."
}

$launchArgs = @('--game', $installPath, '--map', $Map, '--start', $Start,
                '--renderer', $Renderer)
if (-not $FreeCamera) {
    $launchArgs += '--walk'
}
if ($DebugAreas) { $launchArgs += '--debug-areas' }
if ($ScriptCheck) { $launchArgs += @('--script-check', '--script-steps', $ScriptSteps.ToString()) }
if ($PSBoundParameters.ContainsKey('Frames')) {
    $launchArgs += @('--frames', $Frames.ToString())
}

Write-Host "Launching $Map at $Start ($Renderer renderer)."
Push-Location -LiteralPath (Split-Path -Parent $executable)
try {
    # Piping output also makes PowerShell wait for the windowed executable.
    & $executable @launchArgs | Out-Host
    $launchExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}
exit $launchExitCode
