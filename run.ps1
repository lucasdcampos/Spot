# Builds and runs the sandbox or the editor: ./run.ps1 [sandbox|editor] [debug|release]
$target = "sandbox"
$config = "debug"
foreach ($arg in $args) {
    if ($arg -in "sandbox", "editor") { $target = $arg }
    elseif ($arg -in "debug", "release") { $config = $arg }
    else {
        Write-Error "Unknown argument '$arg': expected sandbox, editor, debug or release"
        exit 1
    }
}
$cmakeTarget = if ($target -eq "editor") { "spot_editor" } else { "sandbox" }

Push-Location $PSScriptRoot
try {
    # Configure only once; later builds re-run CMake by themselves when the CMake files change.
    if (-not (Test-Path build/msvc/CMakeCache.txt)) {
        cmake --preset msvc
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    cmake --build --preset "msvc-$config" --target $cmakeTarget
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & "build/msvc/$target/$config/$cmakeTarget.exe"
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
