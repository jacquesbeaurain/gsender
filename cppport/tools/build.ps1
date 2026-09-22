<#
.SYNOPSIS
    Configures, builds and optionally tests the C++ port from any PowerShell.

.DESCRIPTION
    Enters the Visual Studio developer environment (so Ninja finds cl.exe),
    points the presets at the FreeCAD LibPacks and runs the requested steps.

    Built for a fast edit-build-test loop:
      * The developer environment is captured once into build/.vsdevenv.json
        and replayed on later runs (entering it takes over a second).
      * Tests run by executing each *_tests.exe directly - one process for the
        whole suite - instead of CTest, which starts a process per test case.
      * Output is brief by default: build errors/warnings, test failures and a
        one-line summary per phase. -Full shows everything.

    LibPack locations come from GS_LIBPACK_DEBUG / GS_LIBPACK_RELEASE when set,
    otherwise from the defaults below.

.EXAMPLE
    ./tools/build.ps1 -Test                        # build all, run every test
    ./tools/build.ps1 -Filter 'Controller*'        # build all, run matching tests
    ./tools/build.ps1 -Target gs_core              # compile the library only
    ./tools/build.ps1 -Config release -Test        # release build (no PCH)
    ./tools/build.ps1 -CTest -TestRegex Sender     # through CTest
#>
param(
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',
    [string[]]$Target,
    # Run the test executables directly (fast).
    [switch]$Test,
    # GoogleTest filter, e.g. 'Controller*:Sender.*-*Slow*'; implies -Test.
    [string]$Filter,
    # Run the tests through CTest instead (a process per test case).
    [switch]$CTest,
    # CTest -R regex; implies -CTest.
    [string]$TestRegex,
    # Show the complete build and test output.
    [switch]$Full,
    [switch]$Reconfigure,
    [switch]$Clean,
    # Enter the developer environment afresh and refresh its cache.
    [switch]$RefreshVsEnv
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Filter) { $Test = $true }
if ($TestRegex) { $CTest = $true }

if (-not $env:GS_LIBPACK_DEBUG) {
    $env:GS_LIBPACK_DEBUG = 'D:\repos\oth\FreeCADLibs\LibPack-26.3.0-v3.5.5-x64-Debug'
}
if (-not $env:GS_LIBPACK_RELEASE) {
    $env:GS_LIBPACK_RELEASE = 'D:\repos\oth\FreeCADLibs\LibPack-26.3.0-v3.5.5-x64-Release'
}

function Enter-VsDevEnvironment {
    if ($env:VSCMD_VER -and -not $RefreshVsEnv) { return }
    $cacheFile = Join-Path $root 'build/.vsdevenv.json'

    # Replay the cached environment while the compiler it points at exists
    # (a Visual Studio update moves cl.exe and so invalidates the cache).
    if (-not $RefreshVsEnv -and (Test-Path $cacheFile)) {
        $cache = Get-Content $cacheFile -Raw | ConvertFrom-Json
        if ($cache.cl -and (Test-Path $cache.cl)) {
            foreach ($var in $cache.vars.PSObject.Properties) {
                Set-Item -Path "env:$($var.Name)" -Value $var.Value
            }
            $env:PATH = (@($cache.pathPrefix) + @($env:PATH)) -join ';'
            return
        }
    }

    $before = @{}
    Get-ChildItem env: | ForEach-Object { $before[$_.Name] = $_.Value }
    $oldPath = @($env:PATH -split ';')

    $installerDir = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    # VsDevCmd itself shells out to vswhere; make sure it can find it.
    $env:PATH = "$installerDir;$env:PATH"
    $vsPath = & (Join-Path $installerDir 'vswhere.exe') -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'Visual Studio with the C++ toolset was not found' }
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

    # Cache what changed: PATH as the entries it gained, the rest verbatim.
    $vars = [ordered]@{}
    Get-ChildItem env: | Where-Object { $_.Name -ne 'Path' -and $before[$_.Name] -ne $_.Value } |
        ForEach-Object { $vars[$_.Name] = $_.Value }
    $pathPrefix = @($env:PATH -split ';' | Where-Object { $_ -and $oldPath -notcontains $_ })
    $cl = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source
    New-Item -ItemType Directory -Force (Split-Path $cacheFile) | Out-Null
    [ordered]@{ cl = $cl; pathPrefix = $pathPrefix; vars = $vars } |
        ConvertTo-Json -Depth 4 | Set-Content -Path $cacheFile -Encoding utf8
}

# Runs a native command. Brief mode prints only diagnostics (all output when
# the command fails without any); -Full passes everything through.
function Invoke-Step([string]$Name, [scriptblock]$Command, [string]$Pattern) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    if ($Full) {
        & $Command
        $code = $LASTEXITCODE
    } else {
        $output = @(& $Command 2>&1 | ForEach-Object { "$_" })
        $code = $LASTEXITCODE
        $shown = @($output | Where-Object { $_ -match $Pattern })
        if ($shown.Count) {
            $shown | ForEach-Object { Write-Host $_ }
        } elseif ($code) {
            $output | ForEach-Object { Write-Host $_ }
        }
    }
    $status = if ($code) { "FAILED ($code)" } else { 'ok' }
    Write-Host ("{0}: {1} ({2:N1}s)" -f $Name, $status, $timer.Elapsed.TotalSeconds)
    if ($code) { throw "$Name failed ($code)" }
}

Enter-VsDevEnvironment

$preset = "ninja-$Config"
$buildDir = Join-Path $root "build/$preset"
$diagnostics = '(error|warning) [A-Z]*\d+|: (fatal )?error|^FAILED:|CMake (Error|Warning)|ninja: build stopped'

Push-Location $root
try {
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Recurse -Force $buildDir
    }
    if ($Reconfigure -or -not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        Invoke-Step 'configure' { cmake --preset $preset } 'CMake (Error|Warning)'
    }

    $buildArgs = @('--build', '--preset', $preset)
    if ($Target) { $buildArgs += '--target'; $buildArgs += $Target }
    Invoke-Step 'build' { cmake @buildArgs } $diagnostics

    if ($Test) {
        $gtestArgs = @('--gtest_brief=1')
        if ($Filter) { $gtestArgs += "--gtest_filter=$Filter" }
        $failed = $false
        foreach ($exe in Get-ChildItem (Join-Path $buildDir 'bin') -Filter '*_tests.exe') {
            $timer = [Diagnostics.Stopwatch]::StartNew()
            $output = @(& $exe.FullName @gtestArgs 2>&1 | ForEach-Object { "$_" })
            $code = $LASTEXITCODE
            # Brief GoogleTest output is failures plus the summary; drop the banner.
            $output | Where-Object { $Full -or $_ -notmatch '^Running main\(\) from' } | ForEach-Object { Write-Host $_ }
            $status = if ($code) { "FAILED ($code)" } else { 'ok' }
            Write-Host ("{0}: {1} ({2:N1}s)" -f $exe.BaseName, $status, $timer.Elapsed.TotalSeconds)
            if ($code) { $failed = $true }
        }
        if ($failed) { throw 'Tests failed' }
    }

    if ($CTest) {
        $testArgs = @('--preset', $preset)
        if ($TestRegex) { $testArgs += '-R'; $testArgs += $TestRegex }
        Invoke-Step 'ctest' { ctest @testArgs } '(Failed|\*\*\*|tests passed|tests failed)'
    }
}
finally {
    Pop-Location
}
