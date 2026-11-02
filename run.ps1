# Builds the CLI and runs it in the caller's working directory:
#   ./run.ps1 [debug|release] [no-build] <command> [options]
#   ./run.ps1 new MyGame
#   ./run.ps1 build
#   ./run.ps1 run
$config = "release"
$build = $true
$cliArguments = @()

for ($i = 0; $i -lt $args.Count; $i++) {
    $arg = $args[$i]
    if ($arg -in "debug", "release") {
        $config = $arg
    }
    elseif ($arg -eq "no-build") {
        $build = $false
    }
    else {
        # All remaining arguments belong to the spot CLI command
        $cliArguments = @($args | Select-Object -Skip $i)
        break
    }
}

$cmakeTarget = "spot_cli"
$executableName = "spot"

Push-Location $PSScriptRoot
try {
    if ($build) {
        if (-not (Test-Path build/msvc/CMakeCache.txt)) {
            cmake --preset msvc
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }

        cmake --build --preset "msvc-$config" --target $cmakeTarget
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    $executable = Join-Path $PSScriptRoot "build/msvc/cli/$config/$executableName.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        Write-Error "Executable '$executable' not found. Run './run.ps1' to build it first."
        exit 1
    }
}
finally {
    Pop-Location
}

& $executable @cliArguments
