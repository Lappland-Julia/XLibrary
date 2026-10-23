#requires -Version 7.0
param(
    [string]$BuildPath = 'cmake-build-release',
    [string]$OutputPath = '',
    [switch]$Installer,
    [string]$InnoCompiler = '',
    [string]$MsvcRuntimePath = '',
    [string]$CompilerLicensePath = '',
    [string]$QtSourceManifest = "$PSScriptRoot/qt_sources.json"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = (Resolve-Path -LiteralPath "$PSScriptRoot/..").Path
function Resolve-RepositoryPath([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return [IO.Path]::GetFullPath($path) }
    [IO.Path]::GetFullPath((Join-Path $repository $path))
}
$build = (Resolve-Path -LiteralPath (Resolve-RepositoryPath $BuildPath)).Path
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
function Read-Cache([string]$name, [switch]$Optional) {
    $entry = @($cache | Select-String -Pattern "^$([regex]::Escape($name)):[^=]+=(.*)$")
    if ($entry.Count -eq 0) {
        if ($Optional) { return '' }
        throw "CMake cache entry is missing: $name"
    }
    $entry[0].Matches[0].Groups[1].Value
}

if ((Read-Cache 'CMAKE_PROJECT_NAME') -ne 'XLibrary' -or
    (Resolve-RepositoryPath (Read-Cache 'CMAKE_HOME_DIRECTORY')) -ne $repository) {
    throw 'Build directory must be configured from this XLibrary checkout.'
}
$cmake = Read-Cache 'CMAKE_COMMAND'
# Build может перезапустить configure; обновляем версию и toolchain до выбора имени пакета.
& $cmake -S $repository -B $build
if ($LASTEXITCODE -ne 0) { throw 'Release configuration failed.' }
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
$config = Get-Content -LiteralPath "$build/packaging-config.json" -Raw | ConvertFrom-Json
$compiler = switch ($config.compiler_id) {
    'GNU' { 'mingw' }
    'MSVC' { 'msvc' }
    default { throw "Unsupported release compiler: $($config.compiler_id)" }
}
if ($config.pointer_size -ne '8') { throw 'Distribution requires a Windows x64 build.' }
if ($compiler -eq 'msvc' -and $config.compiler_architecture -ne 'x64') {
    throw 'Distribution requires an x64 MSVC target.'
}
if ($compiler -eq 'mingw') {
    $target_machine = & $config.compiler -dumpmachine
    if ($LASTEXITCODE -ne 0 -or $target_machine -notmatch '^x86_64-.*mingw32$') {
        throw 'Distribution requires an x86_64 MinGW target.'
    }
}
$build_type = Read-Cache 'CMAKE_BUILD_TYPE' -Optional
$configurations = (Read-Cache 'CMAKE_CONFIGURATION_TYPES' -Optional) -split ';'
if ($build_type -ne 'Release' -and 'Release' -notin $configurations) {
    throw 'Use a Release build directory or a multi-config generator with Release.'
}
if ((Read-Cache 'XLIBRARY_NATIVE_CPU') -ne 'OFF') { throw 'Distribution requires XLIBRARY_NATIVE_CPU=OFF.' }
$compiler_bin = Split-Path $config.compiler -Parent
$qt = [IO.Path]::GetFullPath((Join-Path (Read-Cache 'Qt6_DIR') '../../..'))
$qt_version = $config.qt_version
$version = Read-Cache 'CMAKE_PROJECT_VERSION'
$source_manifest = Get-Content -LiteralPath $QtSourceManifest -Raw | ConvertFrom-Json -AsHashtable
if (-not $source_manifest.ContainsKey($qt_version)) {
    throw "Add verified Qt $qt_version source archive SHA256 values to -QtSourceManifest before packaging."
}
if ($Installer) {
    if (-not $InnoCompiler) {
        $iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($iscc) { $InnoCompiler = $iscc.Source }
    }
    if (-not $InnoCompiler -or -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
        throw 'Install Inno Setup 6.3+ and pass -InnoCompiler or put ISCC.exe on PATH.'
    }
}
if (-not $OutputPath) { $OutputPath = "release/v$version-pre-release-$compiler" }
$output = Resolve-RepositoryPath $OutputPath
if (Test-Path -LiteralPath $output) { throw "Output already exists; choose a new -OutputPath: $output" }
$package_name = "XLibrary-$version-pre-release-windows-x64-$compiler-portable"
$stage = Join-Path $repository "build/release-$([guid]::NewGuid().ToString('N'))"
$package = Join-Path $stage $package_name
$saved_path = $env:PATH
$success = $false

try {
    # PATH меняется в процессе PowerShell, а не передаётся списком через MSBuild.
    $env:PATH = "$compiler_bin;$qt/bin;$saved_path"
    & $cmake --build $build --config Release --target XLibrary --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    & $cmake --install $build --config Release --prefix $package
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }

    $licenses = Join-Path $package 'licenses'
    New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    if ($compiler -eq 'mingw') {
        foreach ($dll in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
            $runtime = & $config.compiler "-print-file-name=$dll"
            if ($LASTEXITCODE -eq 0 -and $runtime -eq $dll) {
                $runtime = Join-Path $compiler_bin $dll
            }
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $runtime -PathType Leaf)) {
                throw "Configured MinGW cannot locate $dll"
            }
            Copy-Item -LiteralPath $runtime -Destination $package
        }
        $toolchain = Split-Path $compiler_bin -Parent
        $license_roots = if ($CompilerLicensePath) { @($CompilerLicensePath) } else {
            @("$toolchain/licenses", "$toolchain/share/doc", "$toolchain/share/licenses")
        }
        $gcc_licenses = Join-Path $licenses 'mingw'
        New-Item -ItemType Directory -Path $gcc_licenses | Out-Null
        $index = 0
        foreach ($root in $license_roots) {
            if (Test-Path -LiteralPath $root -PathType Container) {
                Copy-Item -LiteralPath $root -Destination "$gcc_licenses/$index" -Recurse
                ++$index
            }
        }
        foreach ($required in @('COPYING3', 'COPYING.RUNTIME')) {
            if (@(Get-ChildItem -LiteralPath $gcc_licenses -Recurse -File -Filter $required).Count -eq 0) {
                throw "MinGW license $required is missing; pass -CompilerLicensePath containing toolchain notices."
            }
        }
    } else {
        if (-not $MsvcRuntimePath) {
            if (-not $env:VCToolsRedistDir) { throw 'Use an x64 VS developer shell or pass -MsvcRuntimePath.' }
            $MsvcRuntimePath = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
        }
        $runtime_files = @(Get-ChildItem -LiteralPath $MsvcRuntimePath -Filter '*.dll' -File)
        if ($runtime_files.Count -eq 0) { throw "No MSVC CRT DLLs in $MsvcRuntimePath" }
        $runtime_files | Copy-Item -Destination $package
    }

    # Сохраняем исходники распространяемых модулей вместе с лицензиями, включая third-party.
    $modules = [Collections.Generic.HashSet[string]]::new()
    [void]$modules.Add('qtbase')
    $base_libraries = @('Core', 'Gui', 'Widgets', 'Network', 'Concurrent', 'OpenGL', 'OpenGLWidgets',
        'PrintSupport', 'Sql', 'Xml', 'DBus')
    foreach ($dll in Get-ChildItem -LiteralPath $package -Filter 'Qt6*.dll' -File) {
        $module = $dll.BaseName.Substring(3)
        if ($module -in @('Svg', 'SvgWidgets')) { [void]$modules.Add('qtsvg') }
        elseif ($module -notin $base_libraries) { throw "Add source/licensing support for $($dll.Name)." }
    }
    if (@(Get-ChildItem -LiteralPath "$package/plugins/imageformats" -Filter '*.dll' -ErrorAction SilentlyContinue |
            Where-Object { $_.BaseName -in @('qicns', 'qmng', 'qtga', 'qtiff', 'qwbmp', 'qwebp', 'qjp2') }).Count -gt 0) {
        [void]$modules.Add('qtimageformats')
    }
    $sources = Join-Path $package 'sources'
    New-Item -ItemType Directory -Path $sources | Out-Null
    $notices = [Collections.Generic.List[string]]::new()
    $notices.Add("Qt $qt_version is dynamically linked under LGPLv3; see licenses/qtbase and sources/.")
    $notices.Add('You may replace the Qt DLLs/plugins with compatible modified builds. Reverse engineering for debugging those modifications is permitted.')
    $notices.Add('Qt licensing: https://www.qt.io/licensing/open-source-lgpl-obligations')
    foreach ($module in ($modules | Sort-Object)) {
        $hash = $source_manifest[$qt_version][$module]
        if ($hash -notmatch '^[0-9a-fA-F]{64}$') { throw "Missing verified SHA256 for $module $qt_version" }
        $archive_name = "$module-everywhere-src-$qt_version.tar.xz"
        $url = "https://download.qt.io/archive/qt/$($qt_version -replace '\.\d+$','')/$qt_version/submodules/$archive_name"
        $archive = Join-Path $sources $archive_name
        Invoke-WebRequest -Uri $url -OutFile $archive
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $hash) {
            throw "Qt source checksum mismatch: $archive_name"
        }
        & tar -xf $archive -C $stage "$module-everywhere-src-$qt_version/LICENSES"
        if ($LASTEXITCODE -ne 0) { throw "Cannot extract $module licenses." }
        Copy-Item -LiteralPath "$stage/$module-everywhere-src-$qt_version/LICENSES" -Destination "$licenses/$module" -Recurse
        $sbom_files = @(Get-ChildItem -Path "$qt/sbom/$module*.spdx*" -File -ErrorAction SilentlyContinue)
        foreach ($sbom in $sbom_files) { Copy-Item -LiteralPath $sbom.FullName -Destination $licenses }
        $notices.Add("`n$module corresponding source: sources/$archive_name`nUpstream: $url`nSHA256: $hash")
    }
    $notices.Add("`nSQLite is statically linked and in the public domain: https://sqlite.org/copyright.html")
    if ($compiler -eq 'mingw') {
        $notices.Add('MinGW runtime: GCC GPLv3 with GCC Runtime Library Exception; see licenses/mingw (including MinGW-w64/winpthreads notices).')
    } else {
        $notices.Add('Microsoft Visual C++ runtime: https://visualstudio.microsoft.com/license-terms/ ; redistribution requires the applicable Visual Studio license.')
    }
    $notices | Set-Content -LiteralPath "$package/THIRD_PARTY_NOTICES.txt" -Encoding utf8

    $commit = git -C $repository rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the source commit.' }
    $changes = @(git -C $repository status --short)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source changes.' }
    @{
        version = $version
        channel = 'pre-release'
        architecture = 'x64'
        configuration = 'Release'
        qt_version = $qt_version
        compiler = $compiler
        compiler_version = $config.compiler_version
        source_commit = $commit
        source_dirty = $changes.Count -gt 0
        source_changes = $changes
        built_at_utc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath "$package/build-info.json" -Encoding utf8

    New-Item -ItemType File -Path "$package/portable.flag" | Out-Null
    @"
XLibrary $version pre-release - Windows x64 ($compiler)

Run XLibrary.exe. Keep the DLLs and plugins beside it.
No Qt installation or administrator rights are required.
portable.flag enables portable mode: database and settings use data/.
Keep this folder writable. Move the whole folder to move the program and data.
The installer uses the Windows user profile and preserves data on uninstall.
This pre-release has no digital signature. See licenses/, sources/ and THIRD_PARTY_NOTICES.txt.
"@ | Set-Content -LiteralPath "$package/README.txt" -Encoding utf8
    & "$repository/.github/scripts/check_package.ps1" -PackagePath $package -Compiler $compiler
    New-Item -ItemType Directory -Path $output | Out-Null
    if ($Installer) {
        & $InnoCompiler /Qp "/DAppVersion=$version" "/DCompiler=$compiler" "/DPackageDir=$package" "/DOutputDir=$output" "$PSScriptRoot/xlibrary.iss"
        if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
    }
    Compress-Archive -LiteralPath $package -DestinationPath "$output/$package_name.zip" -CompressionLevel Optimal
    Get-ChildItem -LiteralPath $output -File | Where-Object { $_.Extension -in '.exe', '.zip' } |
        Sort-Object Name | ForEach-Object {
            "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
        } | Set-Content -LiteralPath "$output/SHA256SUMS.txt" -Encoding ascii
    $success = $true
    Write-Output "Release prepared: $output"
} finally {
    $env:PATH = $saved_path
    if ($success) { Remove-Item -LiteralPath $stage -Recurse -Force }
    else { Write-Warning "Packaging failed. Staging files, if created, remain at $stage" }
}
