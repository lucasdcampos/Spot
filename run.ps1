# Builds and runs: ./run.ps1 [sandbox|editor] [debug|release] [no-build]
$target = "sandbox"
$config = "release"
$build = $true
foreach ($arg in $args) {
    if ($arg -in "sandbox", "editor") { $target = $arg }
    elseif ($arg -in "debug", "release") { $config = $arg }
    elseif ($arg -eq "no-build") { $build = $false }
    else {
        Write-Error "Unknown argument '$arg': expected sandbox, editor, debug, release or no-build"
        exit 1
    }
}
$cmakeTarget = if ($target -eq "editor") { "spot_editor" } else { "sandbox" }

Push-Location $PSScriptRoot
try {
    if ($build) {
        # Configure only once; later builds re-run CMake when its files change.
        if (-not (Test-Path build/msvc/CMakeCache.txt)) {
            cmake --preset msvc
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }

        cmake --build --preset "msvc-$config" --target $cmakeTarget
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    $executable = "build/msvc/$target/$config/$cmakeTarget.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        Write-Error "Executable '$executable' not found. Run './run.ps1 $target $config' to build it first."
        exit 1
    }
    & $executable
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
