# Runs targeted tests during development to avoid full rebuilds and whole-suite test runs:
#   ./scripts/test.ps1 [debug|release] [no-build] [msvc|ninja] [-t|--target <target>] [filter] [options...]
#
# Examples:
#   ./scripts/test.ps1 "[math]"
#   ./scripts/test.ps1 "Transform*"
#   ./scripts/test.ps1 -t editor "ConsoleLog*"
#   ./scripts/test.ps1 no-build "[scene]"
#   ./scripts/test.ps1 -t gpu

$config = "debug"
$preset = ""
$build = $true
$target = "spot_tests"
$testArgs = @()

$targetAliases = @{
    "main"          = "spot_tests"
    "tests"         = "spot_tests"
    "spot"          = "spot_tests"
    "editor"        = "spot_editor_tests"
    "editor-gpu"    = "spot_editor_gpu_tests"
    "editor-module" = "spot_editor_module_tests"
    "editor-tools"  = "spot_editor_tools_tests"
    "gpu"           = "spot_gpu_tests"
    "anim"          = "spot_animation_tests"
    "animation"     = "spot_animation_tests"
    "ui"            = "spot_ui_tests"
    "particle"      = "spot_particle_tests"
    "particles"     = "spot_particle_tests"
    "gameplay"      = "spot_gameplay_tests"
    "console"       = "spot_console_tests"
    "luau"          = "spot_luau_tests"
    "luau-shared"   = "spot_luau_shared_tests"
    "script"        = "spot_script_tests"
    "script-asset"  = "spot_script_asset_tests"
    "script-shared" = "spot_script_shared_tests"
    "profile"       = "spot_profile_process_tests"
}

$i = 0
while ($i -lt $args.Count) {
    $arg = $args[$i]
    if ($arg -in "debug", "release") {
        $config = $arg
    }
    elseif ($arg -eq "no-build") {
        $build = $false
    }
    elseif ($arg -in "msvc", "ninja") {
        $preset = $arg
    }
    elseif ($arg -in "-t", "--target") {
        $i++
        if ($i -lt $args.Count) {
            $rawTarget = $args[$i]
            if ($targetAliases.ContainsKey($rawTarget)) {
                $target = $targetAliases[$rawTarget]
            } else {
                $target = $rawTarget
            }
        }
    }
    else {
        # Check if the argument is a known target alias
        if ($targetAliases.ContainsKey($arg)) {
            $target = $targetAliases[$arg]
        } else {
            # Everything else belongs to Catch2
            $testArgs = @($args | Select-Object -Skip $i)
            break
        }
    }
    $i++
}

# Resolve target name if missing prefix
if (-not $target.StartsWith("spot_")) {
    $target = "spot_$target"
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Resolve-Path (Join-Path $scriptDir "..")

# Auto-detect preset if unspecified
if (-not $preset) {
    $isWin = $IsWindows -or ($env:OS -match "Windows")
    if ($isWin) {
        if (Test-Path (Join-Path $projectRoot "build/msvc/CMakeCache.txt")) {
            $preset = "msvc"
        } elseif (Test-Path (Join-Path $projectRoot "build/ninja/CMakeCache.txt")) {
            $preset = "ninja"
        } else {
            $preset = "msvc"
        }
    } else {
        if (Test-Path (Join-Path $projectRoot "build/ninja/CMakeCache.txt")) {
            $preset = "ninja"
        } else {
            $preset = "ninja"
        }
    }
}

Push-Location $projectRoot
try {
    if ($build) {
        $cacheFile = Join-Path $projectRoot "build/$preset/CMakeCache.txt"
        if (-not (Test-Path $cacheFile)) {
            cmake --preset $preset
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }

        cmake --build --preset "$preset-$config" --target $target
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    $configDir = if ($config -eq "release") { "Release" } else { "Debug" }
    $candidatePaths = @(
        (Join-Path $projectRoot "build/$preset/tests/$configDir/$target.exe"),
        (Join-Path $projectRoot "build/$preset/tests/$target.exe"),
        (Join-Path $projectRoot "build/$preset/tests/$configDir/$target"),
        (Join-Path $projectRoot "build/$preset/tests/$target")
    )

    $executable = $null
    foreach ($cand in $candidatePaths) {
        if (Test-Path -LiteralPath $cand -PathType Leaf) {
            $executable = $cand
            break
        }
    }

    if (-not $executable) {
        Write-Error "Test executable for target '$target' not found in build directory ($candidatePaths[0])."
        exit 1
    }

    # Headless video driver unless running GPU tests or already set
    $previousDriver = $env:SDL_VIDEO_DRIVER
    $isGpuTarget = ($target -match "gpu")
    if (-not $isGpuTarget -and -not $env:SDL_VIDEO_DRIVER) {
        $env:SDL_VIDEO_DRIVER = "dummy"
    }

    try {
        & $executable @testArgs
        exit $LASTEXITCODE
    }
    finally {
        if (-not $previousDriver) {
            Remove-Item env:\SDL_VIDEO_DRIVER -ErrorAction SilentlyContinue
        } else {
            $env:SDL_VIDEO_DRIVER = $previousDriver
        }
    }
}
finally {
    Pop-Location
}
