#requires -Version 7.0
<#
.SYNOPSIS
Build Windows/Linux packages in release/v<VERSION> with one SHA256 manifest.
.DESCRIPTION
Uses the existing platform packagers and configured Release build directories.
Linux packaging runs in WSL. Relative Windows paths are based on the repository;
LinuxBuildPath is relative to the repository in WSL or an absolute Linux path.
.EXAMPLE
pwsh -File ./packaging/package_project.ps1
.EXAMPLE
pwsh -File ./packaging/package_project.ps1 -Platform Windows -WhatIf
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateSet('All', 'Windows', 'Linux')]
    [string]$Platform = 'All',
    [string]$WindowsBuildPath = 'cmake-build-release',
    [string]$LinuxBuildPath = 'cmake-build-linux-release',
    [string]$OutputRoot = 'release',
    [string]$WslDistribution = '',
    [string]$InnoCompiler = '',
    [string]$MsvcRuntimePath = '',
    [string]$CompilerLicensePath = '',
    [string]$QtSourceManifest = "$PSScriptRoot/qt_sources.json",
    [string]$QtSourceCache = 'build/windows-packaging-cache',
    [string]$ReleaseNotes = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = (Resolve-Path -LiteralPath "$PSScriptRoot/..").Path

function resolve_repository_path([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return [IO.Path]::GetFullPath($path) }
    return [IO.Path]::GetFullPath((Join-Path $repository $path))
}

function invoke_wsl([string[]]$command_arguments) {
    & wsl.exe @wsl_arguments --exec @command_arguments
    if ($LASTEXITCODE -ne 0) { throw "WSL command failed: $($command_arguments[0]) (exit $LASTEXITCODE)" }
}

function convert_to_wsl_path([string]$path) {
    $converted = @(invoke_wsl -command_arguments @('wslpath', '-a', '-u', $path))
    if ($converted.Count -ne 1 -or -not $converted[0].StartsWith('/')) {
        throw "Cannot convert path for WSL: $path"
    }
    return $converted[0]
}

$version = (Get-Content -LiteralPath "$repository/VERSION" -Raw).Trim()
if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'VERSION must contain a version in major.minor.patch format.'
}
$output = Join-Path (resolve_repository_path $OutputRoot) "v$version"
if (Test-Path -LiteralPath $output) { throw "Output already exists; choose another -OutputRoot: $output" }
if (-not $IsWindows) { throw 'Run this script in PowerShell 7 on Windows; Linux packages are built through WSL.' }

if (-not $PSCmdlet.ShouldProcess($output, "Build $Platform packages for XLibrary $version and write SHA256SUMS.txt")) {
    return
}

$build_windows = $Platform -in @('All', 'Windows')
$build_linux = $Platform -in @('All', 'Linux')
$windows_parameters = @{}
$wsl_arguments = @()
if ($ReleaseNotes) {
    $notes = (Resolve-Path -LiteralPath (resolve_repository_path $ReleaseNotes)).Path
    if (-not (Test-Path -LiteralPath $notes -PathType Leaf)) { throw 'ReleaseNotes must be a file.' }
}

# Проверяем обе среды до начала длительной сборки.
if ($build_windows) {
    $windows_build = resolve_repository_path $WindowsBuildPath
    if (-not (Test-Path -LiteralPath "$windows_build/CMakeCache.txt" -PathType Leaf)) {
        throw "Configure a Windows Release build first: $windows_build"
    }
    $config = Get-Content -LiteralPath "$windows_build/packaging-config.json" -Raw | ConvertFrom-Json
    $compiler = switch ($config.compiler_id) {
        'GNU' { 'mingw' }
        'MSVC' { 'msvc' }
        default { throw "Unsupported Windows compiler: $($config.compiler_id)" }
    }
    if (-not $InnoCompiler) {
        $iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($iscc) { $InnoCompiler = $iscc.Source }
        else {
            $install_roots = @("$env:LOCALAPPDATA/Programs", ${env:ProgramFiles(x86)}, $env:ProgramFiles)
            foreach ($install_root in $install_roots) {
                if (-not $install_root) { continue }
                foreach ($inno_version in @('7', '6')) {
                    $candidate = Join-Path $install_root "Inno Setup $inno_version/ISCC.exe"
                    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                        $InnoCompiler = $candidate
                        break
                    }
                }
                if ($InnoCompiler) { break }
            }
        }
    }
    if (-not $InnoCompiler -or -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
        throw 'Install Inno Setup 6.3+ and pass -InnoCompiler or put ISCC.exe on PATH.'
    }
    $windows_output = Join-Path $output "windows-$compiler"
    $windows_parameters = @{
        BuildPath = $windows_build
        OutputPath = $windows_output
        Installer = $true
        InnoCompiler = $InnoCompiler
        MsvcRuntimePath = $MsvcRuntimePath
        CompilerLicensePath = $CompilerLicensePath
        QtSourceManifest = resolve_repository_path $QtSourceManifest
        QtSourceCache = resolve_repository_path $QtSourceCache
    }
}
if ($build_linux) {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) { throw 'Install WSL with a configured Linux build first.' }
    if ($WslDistribution) { $wsl_arguments = @('--distribution', $WslDistribution) }
    $linux_repository = convert_to_wsl_path $repository
    $linux_output = convert_to_wsl_path (Join-Path $output 'linux-x86_64')
    $linux_build = if ($LinuxBuildPath.StartsWith('/')) { $LinuxBuildPath } else {
        convert_to_wsl_path (resolve_repository_path $LinuxBuildPath)
    }
    invoke_wsl -command_arguments @('test', '-f', "$linux_build/CMakeCache.txt")
    invoke_wsl -command_arguments @('python3', '--version')
    invoke_wsl -command_arguments @('cmake', '--version')
    try {
        invoke_wsl -command_arguments @('pkg-config', '--exists', 'libsecret-1')
    } catch {
        throw 'Linux packaging requires pkg-config and libsecret-1 development files. In the selected WSL distribution run: sudo apt-get install pkg-config libsecret-1-dev'
    }
}

$package_paths = [Collections.Generic.List[string]]::new()
if ($build_windows) {
    & "$PSScriptRoot/build_release.ps1" @windows_parameters
    if (-not $?) { throw 'Windows packaging failed.' }
    $package_paths.Add("windows-$compiler/XLibrary-$version-pre-release-windows-x64-$compiler-portable.zip")
    $package_paths.Add("windows-$compiler/XLibrary-$version-pre-release-windows-x64-$compiler-setup.exe")
}
if ($build_linux) {
    invoke_wsl -command_arguments @('python3', "$linux_repository/packaging/build_release.py",
        '--build-path', $linux_build, '--output-path', $linux_output, '--format', 'all')
    $package_paths.Add("linux-x86_64/XLibrary-$version-linux-x86_64.AppImage")
    $package_paths.Add("linux-x86_64/xlibrary_${version}_amd64.deb")
}
if ((Get-Content -LiteralPath "$repository/VERSION" -Raw).Trim() -ne $version) {
    throw 'VERSION changed during packaging; no combined manifest was created.'
}
foreach ($relative_path in $package_paths) {
    if (-not (Test-Path -LiteralPath (Join-Path $output $relative_path) -PathType Leaf)) {
        throw "Expected package is missing: $relative_path"
    }
}
if ($ReleaseNotes) { Copy-Item -LiteralPath $notes -Destination "$output/RELEASE_NOTES.md" }

# Общий манифест заменяет суммы отдельных платформ и включает также метаданные.
if ($build_windows) { Remove-Item -LiteralPath "$windows_output/SHA256SUMS.txt" }
if ($build_linux) { Remove-Item -LiteralPath "$output/linux-x86_64/SHA256SUMS.txt" }
$manifest_lines = @(Get-ChildItem -LiteralPath $output -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
    $relative_path = [IO.Path]::GetRelativePath($output, $_.FullName).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative_path"
})
[IO.File]::WriteAllText("$output/SHA256SUMS.txt", ($manifest_lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
Write-Output "Packages prepared: $output"
Write-Output "SHA256 manifest: $output/SHA256SUMS.txt ($($manifest_lines.Count) files)"
