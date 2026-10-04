param(
    [ValidateSet("debug", "release")]
    [string]$Config = "debug"
)

Push-Location $PSScriptRoot
try {
    # Configure only once; later builds re-run CMake by themselves when the CMake files change.
    if (-not (Test-Path build/msvc/CMakeCache.txt)) {
        cmake --preset msvc
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    cmake --build --preset "msvc-$Config" --target sandbox
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & "build/msvc/sandbox/$Config/sandbox.exe"
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
