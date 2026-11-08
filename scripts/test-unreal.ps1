param(
    [Parameter(Mandatory=$true)][string]$EngineRoot,
    [Parameter(Mandatory=$true)][string]$SdkPrefix,
    [Parameter(Mandatory=$true)][string]$WorkDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$engineDirectory = (Resolve-Path -LiteralPath $EngineRoot).Path
$sdkDirectory = (Resolve-Path -LiteralPath $SdkPrefix).Path
if (-not [IO.Path]::IsPathRooted($WorkDirectory)) { throw 'WorkDirectory must be absolute.' }
$testDirectory = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $testDirectory) { throw 'Choose a new WorkDirectory; existing files are never replaced.' }
$editor = Join-Path $engineDirectory 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$ubt = Join-Path $engineDirectory 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
$dotnet = Get-ChildItem -Path (Join-Path $engineDirectory 'Engine/Binaries/ThirdParty/DotNet/*/win-x64/dotnet.exe') |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $dotnet -or -not (Test-Path -LiteralPath $editor) -or -not (Test-Path -LiteralPath $ubt)) {
    throw 'EngineRoot must contain an installed Windows Unreal editor, UBT and bundled .NET runtime.'
}
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakeExecutable = $cmakeCommand.Source }
else {
    $vswhereExecutable = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhereExecutable)) { throw 'Install CMake 3.24+.' }
    $vsInstallation = & $vswhereExecutable -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakeExecutable = Join-Path $vsInstallation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$pluginDirectory = Join-Path $testDirectory 'Plugins/CySynck'
& $cmakeExecutable "-DCYSYNCK_PREFIX=$sdkDirectory" "-DUNREAL_PLUGIN_DIR=$pluginDirectory" -P (Join-Path $projectRoot 'integrations/unreal/StageCySynck.cmake')
if ($LASTEXITCODE) { throw 'Unreal plugin staging failed.' }
$buildVersion = Get-Content -LiteralPath (Join-Path $engineDirectory 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
$projectFile = Join-Path $testDirectory 'CySynckSmoke.uproject'
$projectDescription = @{
    FileVersion=3
    EngineAssociation=('{0}.{1}' -f $buildVersion.MajorVersion,$buildVersion.MinorVersion)
    Plugins=@(@{Name='CySynck';Enabled=$true})
} | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText($projectFile, $projectDescription, (New-Object Text.UTF8Encoding($false)))
$previousCliHome = [Environment]::GetEnvironmentVariable('DOTNET_CLI_HOME','Process')
$previousLocalDdc = [Environment]::GetEnvironmentVariable('UE-LocalDataCachePath','Process')
$previousSharedDdc = [Environment]::GetEnvironmentVariable('UE-SharedDataCachePath','Process')
try {
    [Environment]::SetEnvironmentVariable('DOTNET_CLI_HOME',(Join-Path $testDirectory 'dotnet'),'Process')
    [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath',(Join-Path $testDirectory 'DerivedDataCache'),'Process')
    [Environment]::SetEnvironmentVariable('UE-SharedDataCachePath','None','Process')
    & $dotnet $ubt UnrealEditor Win64 Development "-Project=$projectFile" -NoEngineChanges -NoHotReload -NoUBA -NoUBALocal -NoXGE -MaxParallelActions=2 "-Log=$(Join-Path $testDirectory 'ubt.log')" *> (Join-Path $testDirectory 'build.log')
    if ($LASTEXITCODE) { throw "Unreal build failed; inspect $testDirectory/build.log" }
    & $editor $projectFile -unattended -NullRHI -ddc=InstalledNoZenLocalFallback -nosound -NoSplash -NoLiveCoding -NoCrashDialog '-ExecCmds=Automation RunTests CySynck.Native' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$(Join-Path $testDirectory 'AutomationReport')" "-AbsLog=$(Join-Path $testDirectory 'runtime.log')" -stdout -FullStdOutLogOutput -UTF8Output *> (Join-Path $testDirectory 'automation.log')
    if ($LASTEXITCODE) { throw "Unreal automation failed; inspect $testDirectory/automation.log" }
    $reportFile = Join-Path $testDirectory 'AutomationReport/index.json'
    if (-not (Test-Path -LiteralPath $reportFile)) { throw 'Unreal did not write an automation report.' }
    $report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
    if ($report.failed -ne 0 -or $report.notRun -ne 0 -or @($report.tests).Count -lt 5 -or
        @($report.tests | Where-Object { $_.state -ne 'Success' }).Count -ne 0) {
        throw "Unreal tests did not all pass; inspect $reportFile"
    }
    Write-Output ('PASS Unreal {0}.{1}.{2}: {3} CySynck tests; report: {4}' -f $buildVersion.MajorVersion,$buildVersion.MinorVersion,$buildVersion.PatchVersion,@($report.tests).Count,$reportFile)
}
finally {
    [Environment]::SetEnvironmentVariable('DOTNET_CLI_HOME',$previousCliHome,'Process')
    [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath',$previousLocalDdc,'Process')
    [Environment]::SetEnvironmentVariable('UE-SharedDataCachePath',$previousSharedDdc,'Process')
}
