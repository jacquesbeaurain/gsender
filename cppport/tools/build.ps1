<#
.SYNOPSIS
    Configures, builds and optionally tests the C++ port from any PowerShell.

.DESCRIPTION
    Enters the Visual Studio developer environment (so Ninja finds cl.exe),
    points the presets at the FreeCAD LibPacks and runs the requested steps.

    LibPack locations come from GS_LIBPACK_DEBUG / GS_LIBPACK_RELEASE when set,
    otherwise from the defaults below.

.EXAMPLE
    ./tools/build.ps1 -Config debug -Test
    ./tools/build.ps1 -Config release -Target gsender
    ./tools/build.ps1 -Config debug -Test -TestRegex Sender
#>
param(
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',
    [string[]]$Target,
    [switch]$Test,
    [string]$TestRegex,
    [switch]$Reconfigure,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $env:GS_LIBPACK_DEBUG) {
    $env:GS_LIBPACK_DEBUG = 'D:\repos\oth\FreeCADLibs\LibPack-26.3.0-v3.5.5-x64-Debug'
}
if (-not $env:GS_LIBPACK_RELEASE) {
    $env:GS_LIBPACK_RELEASE = 'D:\repos\oth\FreeCADLibs\LibPack-26.3.0-v3.5.5-x64-Release'
}

# Enter the VS developer shell once per process.
if (-not $env:VSCMD_VER) {
    $installerDir = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    $vswhere = Join-Path $installerDir 'vswhere.exe'
    # VsDevCmd itself shells out to vswhere; make sure it can find it.
    $env:PATH = "$installerDir;$env:PATH"
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'Visual Studio with the C++ toolset was not found' }
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

$preset = "ninja-$Config"
$buildDir = Join-Path $root "build/$preset"

Push-Location $root
try {
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Recurse -Force $buildDir
    }
    if ($Reconfigure -or -not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        cmake --preset $preset
        if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }
    }

    $buildArgs = @('--build', '--preset', $preset)
    if ($Target) { $buildArgs += '--target'; $buildArgs += $Target }
    cmake @buildArgs
    if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }

    if ($Test) {
        $testArgs = @('--preset', $preset)
        if ($TestRegex) { $testArgs += '-R'; $testArgs += $TestRegex }
        ctest @testArgs
        if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)" }
    }
}
finally {
    Pop-Location
}
