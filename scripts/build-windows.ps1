param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Headless,
    [string]$RaylibSource = '',
    [string]$AsioSource = '',
    [string]$JsonSource = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$command = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($command) { $command.Source } else { $null }
if (-not $cmakePath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        foreach ($installation in (& $vswhere -products '*' -all -property installationPath)) {
            $candidate = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path -LiteralPath $candidate) { $cmakePath = $candidate; break }
        }
    }
}
if (-not $cmakePath) { throw 'Install CMake 3.20+ or Visual Studio C++ CMake tools.' }
$buildPath = Join-Path $projectRoot $(if ($Headless) { 'build-headless' } else { 'build' })
$arguments = @('-S', $projectRoot, '-B', $buildPath, '-G', 'Visual Studio 17 2022', '-A', 'x64')
$arguments += '-DDUEL_BUILD_CLIENT=' + $(if ($Headless) { 'OFF' } else { 'ON' })
if ($RaylibSource) { $arguments += '-DFETCHCONTENT_SOURCE_DIR_RAYLIB=' + (Resolve-Path -LiteralPath $RaylibSource).Path }
if ($AsioSource) { $arguments += '-DFETCHCONTENT_SOURCE_DIR_ASIO=' + (Resolve-Path -LiteralPath $AsioSource).Path }
if ($JsonSource) { $arguments += '-DFETCHCONTENT_SOURCE_DIR_JSON=' + (Resolve-Path -LiteralPath $JsonSource).Path }
& $cmakePath @arguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmakePath --build $buildPath --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
& $ctestPath --test-dir $buildPath -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
Write-Host "Executables: $buildPath\$Configuration"
