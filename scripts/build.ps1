param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$ServerOnly)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakeExecutable = $cmakeCommand.Source }
else {
    $vswhereExecutable = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhereExecutable)) { throw 'Install CMake 3.24+ and a C++20 toolchain.' }
    $installationDirectory = & $vswhereExecutable -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakeExecutable = Join-Path $installationDirectory 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$desktopOption = if ($ServerOnly) { 'OFF' } else { 'ON' }
& $cmakeExecutable -S $projectDirectory -B (Join-Path $projectDirectory 'build') "-DCYSYNCK_DESKTOP=$desktopOption"
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $cmakeExecutable --build (Join-Path $projectDirectory 'build') --config $Configuration --parallel
if ($LASTEXITCODE) { throw 'C++ compilation failed.' }
$ctestExecutable = Join-Path (Split-Path $cmakeExecutable) 'ctest.exe'
& $ctestExecutable --test-dir (Join-Path $projectDirectory 'build') -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Integration tests failed.' }
& $cmakeExecutable --install (Join-Path $projectDirectory 'build') --config $Configuration --prefix (Join-Path $projectDirectory 'dist')
if ($LASTEXITCODE) { throw 'Installation failed.' }
