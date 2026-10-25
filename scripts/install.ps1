# Installs the Spot engine on Windows: the newest release of a channel, one version, or a build of the source, and
# puts its command line, spot, on the user's PATH. From the web, or from a checkout of the repository:
#
#   irm https://raw.githubusercontent.com/lucasdcampos/Spot/main/scripts/install.ps1 | iex
#   & ([scriptblock]::Create((irm https://raw.githubusercontent.com/lucasdcampos/Spot/main/scripts/install.ps1))) -Channel nightly
#   ./scripts/install.ps1 -FromSource
#
# -Channel stable|nightly   the releases to follow, stable by default; `spot update` keeps to it
# -Version <version>        that release instead of the newest, as 0.1.0
# -Prefix <folder>          where to install, %LOCALAPPDATA%\Spot by default
# -FromSource               builds the engine instead of downloading it: from -Source, the checkout this script is in,
#                           or else a clone in <prefix>-source (of the tag of -Version, if given)
# -Source <checkout>        the checkout to build
# -NoPath                   leaves the PATH as it is
# -Force                    installs even when the same version is installed already
param(
    [ValidateSet('stable', 'nightly')] [string] $Channel = 'stable',
    [string] $Version = '',
    [string] $Prefix = (Join-Path $env:LOCALAPPDATA 'Spot'),
    [switch] $FromSource,
    [string] $Source = '',
    [switch] $NoPath,
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Windows PowerShell draws download progress so slowly that it slows the download down.
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$Repository = 'lucasdcampos/Spot'
$Archive = 'spot-windows-x64.zip'

function Test-Installation([string] $Directory) {
    (Test-Path (Join-Path $Directory 'version.txt')) -and (Test-Path (Join-Path $Directory 'lib/cmake/Spot/SpotConfig.cmake'))
}

function Get-InstalledVersion {
    $file = Join-Path $Prefix 'version.txt'
    if (Test-Path $file) { return (Get-Content $file -Raw).Trim() }
    return ''
}

# Replaces what is in the prefix with the folder's files. Files in use, as spot.exe while `spot update` runs it, can be
# renamed but not deleted, so they are moved aside, and deleted by the next install.
function Install-Tree([string] $From, [string] $To) {
    if (Test-Path $To) {
        $hasFiles = @(Get-ChildItem $To -Force).Count -gt 0
        if ($hasFiles -and -not (Test-Installation $To)) {
            throw "'$To' has files and no Spot installation in it: choose another -Prefix"
        }
        Get-ChildItem $To -Recurse -File -Force | ForEach-Object {
            try {
                Remove-Item $_.FullName -Force
            } catch {
                Move-Item $_.FullName "$($_.FullName).$(Get-Random).old" -Force
            }
        }
    }
    $From = (Resolve-Path $From).Path
    Get-ChildItem $From -Recurse -File -Force | ForEach-Object {
        $target = Join-Path $To $_.FullName.Substring($From.Length).TrimStart('\', '/')
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item $_.FullName $target -Force
    }
}

function Write-InstallInfo([string] $InstalledFrom) {
    Set-Content (Join-Path $Prefix 'install.txt') "channel=$Channel`nsource=$InstalledFrom" -NoNewline
}

# The user's PATH as the registry keeps it, so entries such as %USERPROFILE%\bin stay unexpanded.
function Add-ToPath([string] $Directory) {
    $key = 'HKCU:\Environment'
    $path = (Get-Item $key).GetValue('Path', '', 'DoNotExpandEnvironmentNames')
    $entries = @($path -split ';' | Where-Object { $_ })
    if ($entries -notcontains $Directory) {
        Set-ItemProperty $key -Name Path -Value (($entries + $Directory) -join ';') -Type ExpandString
        # Setting a variable through .NET tells running programs, as Explorer, that the environment changed.
        [Environment]::SetEnvironmentVariable('SPOT_INSTALL', '1', 'User')
        [Environment]::SetEnvironmentVariable('SPOT_INSTALL', $null, 'User')
    }
    if (($env:Path -split ';') -notcontains $Directory) {
        $env:Path = "$Directory;$env:Path"
    }
}

function Find-Checkout {
    if ($Source) { return (Resolve-Path $Source).Path }
    if ($PSScriptRoot -and (Test-Path (Join-Path $PSScriptRoot '../CMakePresets.json'))) {
        return (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    }
    $checkout = "$Prefix-source"
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Building from source needs Git, to clone the engine' }
    if (Test-Path (Join-Path $checkout '.git')) {
        if ($Version) {
            git -C $checkout fetch --depth 1 origin tag "v$Version"
            Invoke-Checked "Fetching v$Version"
            git -C $checkout checkout "v$Version"
            Invoke-Checked "Checking out v$Version"
        } else {
            git -C $checkout pull --ff-only
            Invoke-Checked "Updating '$checkout'"
        }
    } else {
        $branch = 'main'
        if ($Version) { $branch = "v$Version" }
        git clone --depth 1 --branch $branch "https://github.com/$Repository.git" $checkout
        Invoke-Checked "Cloning the engine into '$checkout'"
    }
    return $checkout
}

function Invoke-Checked([string] $What) {
    if ($LASTEXITCODE -ne 0) { throw "$What failed" }
}

# As a file first: Invoke-RestMethod gives files that are not text as bytes in some versions of PowerShell.
function Get-Text([string] $Url) {
    $file = Join-Path $temp 'text'
    Invoke-WebRequest $Url -OutFile $file -UseBasicParsing
    return (Get-Content $file -Raw).Trim()
}

$temp = Join-Path ([IO.Path]::GetTempPath()) "spot-install-$([Guid]::NewGuid())"
New-Item -ItemType Directory $temp | Out-Null
try {
    if ($FromSource) {
        if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'Building from source needs CMake 3.25 or newer on the PATH' }
        $checkout = Find-Checkout
        Write-Host "Building Spot from $checkout"
        # The repository's own presets, so the install shares the build directory of working on the engine.
        Push-Location $checkout
        try {
            cmake --preset msvc
            Invoke-Checked 'Configuring the engine'
            cmake --build --preset msvc-release --target spot_cli spot_editor spot_player glslang-standalone spirv-cross
            Invoke-Checked 'Building the engine'
            $stage = Join-Path $temp 'spot'
            cmake --install build/msvc --config Release --component spot --prefix $stage
            Invoke-Checked 'Installing the engine'
        } finally {
            Pop-Location
        }
        Install-Tree $stage $Prefix
        Write-InstallInfo 'source'
    } else {
        $base = "https://github.com/$Repository/releases/latest/download"
        if ($Version) {
            $base = "https://github.com/$Repository/releases/download/v$Version"
        } elseif ($Channel -eq 'nightly') {
            $base = "https://github.com/$Repository/releases/download/nightly"
        }
        try {
            $latest = Get-Text "$base/version.txt"
        } catch {
            throw "There is no release at $base"
        }
        if (-not $Force -and (Get-InstalledVersion) -eq $latest) {
            Write-Host "Spot $latest is installed already, in $Prefix"
            Write-InstallInfo 'download'
        } else {
            Write-Host "Downloading Spot $latest"
            $zip = Join-Path $temp $Archive
            Invoke-WebRequest "$base/$Archive" -OutFile $zip -UseBasicParsing
            $expected = ((Get-Text "$base/$Archive.sha256") -split '\s+')[0]
            $actual = (Get-FileHash $zip -Algorithm SHA256).Hash
            if ($actual -ne $expected) { throw "The download is not what was released: its SHA-256 is $actual, not $expected" }
            Expand-Archive $zip (Join-Path $temp 'extracted')
            Install-Tree (Join-Path $temp 'extracted/spot') $Prefix
            Write-InstallInfo 'download'
        }
    }
} finally {
    Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
}

$bin = Join-Path $Prefix 'bin'
if (-not $NoPath) { Add-ToPath $bin }
Write-Host "Spot $(Get-InstalledVersion) is installed in $Prefix."
if ($NoPath) {
    Write-Host "Its command line is $bin\spot.exe."
} else {
    Write-Host "Run 'spot help' in a new terminal to start."
}
