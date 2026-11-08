param(
    [Parameter(Mandatory=$true)][string]$EngineRoot,
    [Parameter(Mandatory=$true)][string]$SdkPrefix,
    [Parameter(Mandatory=$true)][string]$WorkDirectory
)
$ErrorActionPreference = 'Stop'
$engineDirectory = (Resolve-Path -LiteralPath $EngineRoot).Path
$sdkDirectory = (Resolve-Path -LiteralPath $SdkPrefix).Path
if (-not [IO.Path]::IsPathRooted($WorkDirectory)) { throw 'WorkDirectory must be absolute.' }
$testDirectory = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $testDirectory) { throw 'Choose a new WorkDirectory; existing files are never replaced.' }
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$projectDirectory = Join-Path $testDirectory 'Project'
$projectFile = Join-Path $projectDirectory 'CySynckSmoke.uproject'
$ubt = Join-Path $engineDirectory 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
$uat = Join-Path $engineDirectory 'Engine/Binaries/DotNET/AutomationTool/AutomationTool.dll'
$dotnet = Get-ChildItem -Path (Join-Path $engineDirectory 'Engine/Binaries/ThirdParty/DotNet/*/win-x64/dotnet.exe') |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $dotnet -or -not (Test-Path -LiteralPath $uat) -or -not (Test-Path -LiteralPath $ubt)) {
    throw 'EngineRoot must contain an installed Windows engine and bundled UBT/UAT/.NET.'
}
function Write-Utf8([string]$Path, [string]$Content) {
    [IO.File]::WriteAllText($Path, $Content, (New-Object Text.UTF8Encoding($false)))
}
function Get-Sha256([string]$Path) {
    # Hashing must work in both Windows PowerShell and pwsh even when their
    # module auto-loading paths differ. OpenRead also preserves Unicode paths.
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
function Assert-Automation([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "Unreal did not write $Path" }
    $report = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($report.failed -ne 0 -or $report.notRun -ne 0 -or @($report.tests).Count -lt 5 -or
        @($report.tests | Where-Object { $_.state -ne 'Success' }).Count -ne 0) {
        throw "Unreal tests did not all pass; inspect $Path"
    }
    return @($report.tests).Count
}
function Quote-NativeArgument([string]$Value) {
    $text = New-Object Text.StringBuilder
    [void]$text.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) { $slashes++; continue }
        if ($character -eq [char]34) {
            [void]$text.Append(('\' * (2*$slashes+1)))
            [void]$text.Append('"')
        } else {
            [void]$text.Append(('\' * $slashes))
            [void]$text.Append($character)
        }
        $slashes = 0
    }
    [void]$text.Append(('\' * (2*$slashes)))
    [void]$text.Append('"')
    return $text.ToString()
}
$overrides = @{
    DOTNET_CLI_HOME=(Join-Path $testDirectory 'dotnet')
    'UE-LocalDataCachePath'=(Join-Path $testDirectory 'DerivedDataCache')
    'UE-SharedDataCachePath'='None'
    uebp_LogFolder=(Join-Path $testDirectory 'UATLogs')
    uebp_FinalLogFolder=(Join-Path $testDirectory 'UATLogs')
    uebp_EngineSavedFolder=(Join-Path $testDirectory 'AutomationSaved')
    CYSYNCK_DOC_ROOT=$null
}
$previous = @{}
foreach ($key in $overrides.Keys) { $previous[$key] = [Environment]::GetEnvironmentVariable($key,'Process') }
try {
    foreach ($key in $overrides.Keys) { [Environment]::SetEnvironmentVariable($key,$overrides[$key],'Process') }
    # First verify the exact staged bridge in an Editor host. This helper refuses
    # engine changes and creates only a fresh isolated project under this run.
    & (Join-Path $PSScriptRoot 'test-unreal.ps1') -EngineRoot $engineDirectory -SdkPrefix $sdkDirectory -WorkDirectory $projectDirectory
    $editorTests = Assert-Automation (Join-Path $projectDirectory 'AutomationReport/index.json')
    $description = Get-Content -LiteralPath $projectFile -Raw | ConvertFrom-Json
    $description | Add-Member -NotePropertyName Modules -NotePropertyValue @(@{Name='CySynckSmoke';Type='Runtime';LoadingPhase='Default'})
    Write-Utf8 $projectFile ($description | ConvertTo-Json -Depth 8)
    $sourceDirectory = Join-Path $projectDirectory 'Source'
    $moduleDirectory = Join-Path $sourceDirectory 'CySynckSmoke'
    New-Item -ItemType Directory -Path $moduleDirectory | Out-Null
    Write-Utf8 (Join-Path $moduleDirectory 'CySynckSmoke.Build.cs') @'
using UnrealBuildTool;
public class CySynckSmoke : ModuleRules {
    public CySynckSmoke(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
    }
}
'@
    Write-Utf8 (Join-Path $moduleDirectory 'CySynckSmoke.cpp') @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, CySynckSmoke, "CySynckSmoke");
'@
    foreach ($target in @(@{Name='CySynckSmoke';Type='Game'},@{Name='CySynckSmokeEditor';Type='Editor'})) {
        $content = @'
using UnrealBuildTool;
public class TARGET_NAME : TargetRules {
    public TARGET_NAME(TargetInfo Target) : base(Target) {
        Type = TargetType.TARGET_TYPE;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("CySynckSmoke");
    }
}
'@
        $content = $content.Replace('TARGET_NAME',$target.Name+'Target').Replace('TARGET_TYPE',$target.Type)
        Write-Utf8 (Join-Path $sourceDirectory ($target.Name+'.Target.cs')) $content
    }
    $configDirectory = Join-Path $projectDirectory 'Config'
    New-Item -ItemType Directory -Path $configDirectory -Force | Out-Null
    Write-Utf8 (Join-Path $configDirectory 'DefaultEngine.ini') @'
[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Engine/Maps/Entry
EditorStartupMap=/Engine/Maps/Entry
ServerDefaultMap=/Engine/Maps/Entry
'@
    Write-Utf8 (Join-Path $configDirectory 'DefaultGame.ini') @'
[/Script/UnrealEd.ProjectPackagingSettings]
bUsePakFile=True
bUseIoStore=False
bUseZenStore=False
IncludePrerequisites=False
MapsToCook=(FilePath="/Engine/Maps/Entry")
'@
    Write-Output 'Building the isolated game/editor modules without engine changes.'
    & $dotnet $ubt CySynckSmokeEditor Win64 Development "-Project=$projectFile" -NoEngineChanges -NoHotReload -NoUBA -NoUBALocal -NoXGE -MaxParallelActions=2 "-Log=$(Join-Path $testDirectory 'ubt-editor.log')" *> (Join-Path $testDirectory 'editor-build.log')
    if ($LASTEXITCODE) { throw "Editor module build failed; inspect $testDirectory/editor-build.log" }
    $archive = Join-Path $testDirectory 'Archive'
    $uatArguments = @($uat,'-NoCompile','BuildCookRun',"-project=$projectFile",'-installed','-nop4',
        '-platform=Win64','-clientconfig=Development','-target=CySynckSmoke','-build','-cook','-stage','-pak','-skipiostore','-archive',
        "-archivedirectory=$archive",'-map=/Engine/Maps/Entry','-unattended','-nocompileeditor',
        '-ubtargs=-NoEngineChanges -NoHotReload -NoUBA -NoUBALocal -NoXGE -MaxParallelActions=2',
        '-AdditionalCookerOptions=-NullRHI -ddc=InstalledNoZenLocalFallback -NoCrashDialog')
    Write-Output 'Cooking and packaging the Development game.'
    & $dotnet @uatArguments *> (Join-Path $testDirectory 'package.log')
    if ($LASTEXITCODE) { throw "Game packaging failed; inspect $testDirectory/package.log" }
    $nativeLibraries = @(Get-ChildItem -LiteralPath $archive -Recurse -File -Filter cysynck_core.dll)
    if ($nativeLibraries.Count -ne 1) { throw 'Expected exactly one staged native CySynck library.' }
    $nativeRoot = Split-Path -Parent (Split-Path -Parent $nativeLibraries[0].FullName)
    $sourceNative = Join-Path $projectDirectory 'Plugins/CySynck/ThirdParty/CySynck'
    $verified = 0
    foreach ($folder in @('bin','Documentation','licenses')) {
        foreach ($file in Get-ChildItem -LiteralPath (Join-Path $sourceNative $folder) -File -Recurse) {
            $relative = $file.FullName.Substring($sourceNative.Length+1)
            $staged = Join-Path $nativeRoot $relative
            if (-not (Test-Path -LiteralPath $staged) -or
                (Get-Sha256 $file.FullName) -ne (Get-Sha256 $staged)) {
                throw "Runtime resource missing or changed in packaged game: $relative"
            }
            $verified++
        }
    }
    foreach ($name in @('LICENSE','THIRD_PARTY.md','version.json')) {
        $staged = Join-Path $nativeRoot $name
        if (-not (Test-Path -LiteralPath $staged) -or
            (Get-Sha256 (Join-Path $sourceNative $name)) -ne (Get-Sha256 $staged)) {
            throw "Missing or changed packaged resource: $name"
        }
        $verified++
    }
    $games = @(Get-ChildItem -LiteralPath $archive -Recurse -File -Filter CySynckSmoke.exe |
        Where-Object { $_.FullName -match '[\\/]Binaries[\\/]Win64[\\/]' })
    if ($games.Count -ne 1) { throw 'Cannot identify the staged Development game executable.' }
    $gameReport = Join-Path $testDirectory 'GameAutomation'
    $gameExecutable = $games[0].FullName
    Write-Output 'Running real packaged-game automation with NullRHI.'
    $gameArguments = @('-unattended','-NullRHI','-ddc=InstalledNoZenLocalFallback','-nosound','-NoSplash','-NoCrashDialog',
        '-ExecCmds=Automation RunTests CySynck.Native','-TestExit=Automation Test Queue Empty',
        "-ReportExportPath=$gameReport","-AbsLog=$(Join-Path $testDirectory 'game-runtime.log')",'-stdout','-FullStdOutLogOutput','-UTF8Output')
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $gameExecutable
    $start.WorkingDirectory = Split-Path -Parent $gameExecutable
    $start.Arguments = (($gameArguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $gameProcess = New-Object Diagnostics.Process
    $gameProcess.StartInfo = $start
    try {
        if (-not $gameProcess.Start()) { throw 'Cannot start the packaged game.' }
        $outputTask = $gameProcess.StandardOutput.ReadToEndAsync()
        $errorTask = $gameProcess.StandardError.ReadToEndAsync()
        $timedOut = -not $gameProcess.WaitForExit(60000)
        if ($timedOut) { $gameProcess.Kill(); [void]$gameProcess.WaitForExit(10000) }
        if (-not $outputTask.Wait(5000) -or -not $errorTask.Wait(5000)) { throw 'Packaged game output did not close.' }
        Write-Utf8 (Join-Path $testDirectory 'game-automation.log') ($outputTask.Result + $errorTask.Result)
        if ($timedOut) { throw 'Packaged game automation exceeded 60 seconds.' }
        if ($gameProcess.ExitCode) { throw "Packaged game runtime failed; inspect $testDirectory/game-runtime.log" }
    } finally { $gameProcess.Dispose() }
    $gameTests = Assert-Automation (Join-Path $gameReport 'index.json')
    $result = @{success=$true;editor_tests=$editorTests;game_tests=$gameTests;verified_runtime_files=$verified;
        game=$games[0].FullName;native_sha256=(Get-Sha256 $nativeLibraries[0].FullName)}
    Write-Utf8 (Join-Path $testDirectory 'result.json') ($result | ConvertTo-Json -Depth 5)
    Write-Output ("PASS real packaged Unreal game: $gameTests automation tests; $verified runtime resource hashes; $testDirectory/result.json")
}
finally {
    foreach ($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key,$previous[$key],'Process') }
}
