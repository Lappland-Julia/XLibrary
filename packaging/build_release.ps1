param(
    [string]$BuildPath = 'cmake-build-release',
    [string]$OutputPath = '',
    [string]$InnoCompiler = "$env:LOCALAPPDATA/Programs/Inno Setup 7/ISCC.exe"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = (Resolve-Path -LiteralPath "$PSScriptRoot/..").Path
$build = (Resolve-Path -LiteralPath (Join-Path $repository $BuildPath)).Path
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
function Read-Cache([string]$name) {
    $entry = $cache | Select-String -Pattern "^$([regex]::Escape($name)):[^=]+=(.*)$"
    if (-not $entry) { throw "CMake cache entry is missing: $name" }
    $entry.Matches[0].Groups[1].Value
}

$cmake = Read-Cache 'CMAKE_COMMAND'
$compiler_bin = Split-Path (Read-Cache 'CMAKE_CXX_COMPILER') -Parent
$qt = [IO.Path]::GetFullPath((Join-Path (Read-Cache 'Qt6_DIR') '../../..'))
$version = Read-Cache 'CMAKE_PROJECT_VERSION'
if ((Read-Cache 'CMAKE_BUILD_TYPE') -ne 'Release') { throw 'Use a Release build directory.' }
if ((Read-Cache 'XLIBRARY_NATIVE_CPU') -ne 'OFF') { throw 'Distribution requires XLIBRARY_NATIVE_CPU=OFF.' }
if (-not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) { throw 'Install Inno Setup 7 or pass -InnoCompiler.' }
if (-not (Test-Path -LiteralPath (Join-Path $compiler_bin 'libstdc++-6.dll'))) {
    throw 'This release script requires the configured MinGW toolchain.'
}
if (-not $OutputPath) { $OutputPath = "release/v$version-pre-release" }
$output = [IO.Path]::GetFullPath((Join-Path $repository $OutputPath))
if (Test-Path -LiteralPath $output) { throw "Output already exists; choose a new -OutputPath: $output" }
$package_name = "XLibrary-$version-pre-release-windows-x64-portable"
$stage = Join-Path $repository "build/release-$([guid]::NewGuid().ToString('N'))"
$package = Join-Path $stage $package_name
$saved_path = $env:PATH

try {
    $env:PATH = "$compiler_bin;$qt/bin;$saved_path"
    & $cmake --build $build --config Release --target XLibrary --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    & $cmake --install $build --config Release --prefix $package
    if ($LASTEXITCODE -ne 0) { throw 'Runtime deployment failed.' }

    $licenses = Join-Path $package 'licenses'
    New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    $compiler_licenses = Join-Path (Split-Path $compiler_bin -Parent) 'licenses'
    foreach ($component in @('gcc', 'winpthreads', 'crt', 'headers')) {
        $source = Join-Path $compiler_licenses $component
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $licenses -Recurse }
    }
    Copy-Item -LiteralPath (Join-Path $compiler_licenses 'gcc/COPYING3') -Destination "$licenses/GPL-3.0.txt"
    Copy-Item -LiteralPath (Join-Path $compiler_licenses 'gcc/COPYING3.LIB') -Destination "$licenses/LGPL-3.0.txt"

    $qt_version = (Get-Item -LiteralPath "$qt/bin/Qt6Core.dll").VersionInfo.ProductVersion -replace '^(\d+\.\d+\.\d+).*$', '$1'
    $notices = [Collections.Generic.List[string]]::new()
    $notices.Add("Qt $qt_version is dynamically linked. Qt libraries can be replaced with compatible builds.")
    $notices.Add('Qt source and licensing: https://www.qt.io/development/download-open-source')
    foreach ($module in @('qtbase', 'qtsvg')) {
        $sbom_path = Join-Path $qt "sbom/$module-$qt_version.spdx.json"
        if (-not (Test-Path -LiteralPath $sbom_path)) { throw "Qt attribution file is missing: $sbom_path" }
        Copy-Item -LiteralPath $sbom_path -Destination $licenses
        $sbom = Get-Content -LiteralPath $sbom_path -Raw | ConvertFrom-Json
        foreach ($license in $sbom.hasExtractedLicensingInfos) {
            $license.extractedText | Set-Content -LiteralPath "$licenses/$($license.licenseId).txt" -Encoding utf8
        }
        foreach ($dependency in $sbom.packages) {
            $notices.Add("`n$($dependency.name): $($dependency.licenseDeclared)`n$($dependency.copyrightText)")
        }
        $notices.Add("`nCorresponding Qt source: https://download.qt.io/archive/qt/$($qt_version -replace '\.\d+$','')/$qt_version/submodules/$module-everywhere-src-$qt_version.tar.xz")
    }
    $notices.Add("`nSQLite is statically linked and in the public domain: https://sqlite.org/copyright.html")
    $notices.Add('MinGW GCC runtime: GPL with GCC Runtime Library Exception; see licenses/gcc.')
    $notices | Set-Content -LiteralPath "$package/THIRD_PARTY_NOTICES.txt" -Encoding utf8

    Push-Location $repository
    try {
        $commit = git rev-parse HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the source commit.' }
        $changes = @(git status --short)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source changes.' }
    } finally { Pop-Location }
    @{
        version = $version
        channel = 'pre-release'
        architecture = 'x64'
        configuration = 'Release'
        qt_version = $qt_version
        compiler = (& (Join-Path $compiler_bin 'g++.exe') -dumpfullversion)
        source_commit = $commit
        source_dirty = $changes.Count -gt 0
        source_changes = $changes
        built_at_utc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath "$package/build-info.json" -Encoding utf8

    & "$repository/.github/scripts/check_package.ps1" -PackagePath $package -Compiler mingw
    New-Item -ItemType Directory -Path $output | Out-Null
    & $InnoCompiler /Qp "/DAppVersion=$version" "/DPackageDir=$package" "/DOutputDir=$output" "$PSScriptRoot/xlibrary.iss"
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }

    New-Item -ItemType File -Path "$package/portable.flag" | Out-Null
    @"
XLibrary $version pre-release - Windows x64

Run XLibrary.exe from this folder. Keep the DLLs and plugins beside it.
No Qt installation or administrator rights are required.
The portable.flag file enables portable mode: database and settings use data/.
Keep this folder writable. Move the whole folder to move the program and data.
The installer uses the Windows user profile instead and preserves data on uninstall.
This pre-release has no digital signature. See licenses/ and THIRD_PARTY_NOTICES.txt.
"@ | Set-Content -LiteralPath "$package/README.txt" -Encoding utf8
    $destination = Join-Path $output $package_name
    Copy-Item -LiteralPath $package -Destination $destination -Recurse
    Compress-Archive -LiteralPath $destination -DestinationPath "$output/$package_name.zip" -CompressionLevel Optimal
    Get-ChildItem -LiteralPath $output -File | Where-Object { $_.Extension -in '.exe', '.zip' } |
        Sort-Object Name | ForEach-Object {
            "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
        } | Set-Content -LiteralPath "$output/SHA256SUMS.txt" -Encoding ascii
    Write-Output "Release prepared: $output"
} finally {
    $env:PATH = $saved_path
}
