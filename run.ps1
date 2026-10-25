# Builds and runs: ./run.ps1 [sandbox|editor] [debug|release] [no-build]
#                  ./run.ps1 [debug|release] [no-build] cli <arguments>
# Everything after cli goes to the command line, which runs in the current folder, as installed it would.
$target = "sandbox"
$config = "release"
$build = $true
$cliArguments = @()
for ($i = 0; $i -lt $args.Count; $i++) {
    $arg = $args[$i]
    if ($arg -eq "cli") {
        $target = "cli"
        $cliArguments = @($args | Select-Object -Skip ($i + 1))
        break
    }
    elseif ($arg -in "sandbox", "editor") { $target = $arg }
    elseif ($arg -in "debug", "release") { $config = $arg }
    elseif ($arg -eq "no-build") { $build = $false }
    else {
        Write-Error "Unknown argument '$arg': expected sandbox, editor, cli, debug, release or no-build"
        exit 1
    }
}
$cmakeTarget = switch ($target) { "editor" { "spot_editor" } "cli" { "spot_cli" } default { "sandbox" } }
$executableName = if ($target -eq "cli") { "spot" } else { $cmakeTarget }

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

    $executable = Join-Path $PSScriptRoot "build/msvc/$target/$config/$executableName.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        Write-Error "Executable '$executable' not found. Run './run.ps1 $config $target' to build it first."
        exit 1
    }
}
finally {
    Pop-Location
}

if ($target -eq "cli") {
    & $executable @cliArguments
} else {
    Push-Location $PSScriptRoot
    try { & $executable } finally { Pop-Location }
}
exit $LASTEXITCODE
