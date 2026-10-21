param(
    [Parameter(Mandatory)]
    [string]$PackagePath,
    [ValidateSet('msvc', 'mingw')]
    [string]$Compiler = 'msvc'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$package = (Resolve-Path -LiteralPath $PackagePath).Path
$required_files = @('XLibrary.exe', 'qt.conf', 'Qt6Core.dll', 'Qt6Gui.dll',
    'Qt6Widgets.dll', 'Qt6Network.dll', 'plugins/platforms/qwindows.dll')
if ($Compiler -eq 'msvc') {
    $required_files += @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
} else {
    $required_files += @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')
}
foreach ($file in $required_files) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $file) -PathType Leaf)) {
        throw "Missing package dependency: $file"
    }
}

$diagnostics = Join-Path $PSScriptRoot '../../build/package-check'
New-Item -ItemType Directory -Force -Path $diagnostics | Out-Null
$diagnostics = (Resolve-Path -LiteralPath $diagnostics).Path
$start_info = [System.Diagnostics.ProcessStartInfo]::new()
$start_info.FileName = Join-Path $package 'XLibrary.exe'
$start_info.Arguments = '--check-startup'
$start_info.WorkingDirectory = $package
$start_info.UseShellExecute = $false
$start_info.RedirectStandardOutput = $true
$start_info.RedirectStandardError = $true

# Исключаем установленный Qt и toolchain из окружения запуска.
$start_info.Environment['PATH'] = "$env:SystemRoot/System32;$env:SystemRoot"
foreach ($name in @($start_info.Environment.Keys)) {
    if ($name -match '^(QT_|QML|QTDIR$)') {
        $start_info.Environment.Remove($name) | Out-Null
    }
}
$start_info.Environment['QT_DEBUG_PLUGINS'] = '1'
$start_info.Environment['QT_FORCE_STDERR_LOGGING'] = '1'
$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $start_info
$started = $false
try {
    $started = $process.Start()
    if (-not $started) { throw 'Failed to start packaged application.' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(45000)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw 'Packaged application did not finish its startup check within 45 seconds.'
    }
    if ($process.ExitCode -ne 0) {
        throw "Packaged application startup check failed with exit code $($process.ExitCode)."
    }
    Write-Output 'Package dependencies, window creation and catalog initialization passed.'
} finally {
    if ($started) {
        if (-not $process.HasExited) {
            $process.Kill($true)
            $process.WaitForExit()
        }
        $stdout.GetAwaiter().GetResult() | Set-Content (Join-Path $diagnostics 'stdout.log') -Encoding utf8
        $stderr.GetAwaiter().GetResult() | Set-Content (Join-Path $diagnostics 'stderr.log') -Encoding utf8
    }
    $process.Dispose()
}
