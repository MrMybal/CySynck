param(
    [ValidateSet('Install','Uninstall','Status')][string]$Action = 'Install',
    [string]$Source = $PSScriptRoot,
    [string]$Destination = (Join-Path $env:LOCALAPPDATA 'Programs\CySynck'),
    [switch]$NoShortcuts,
    [switch]$Portable
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This deployment script requires Windows.' }
function Full([string]$Path) { [IO.Path]::GetFullPath($Path).TrimEnd([char[]]'\/') }
function Within([string]$Root,[string]$Path) {
    $Path.Equals($Root,[StringComparison]::OrdinalIgnoreCase) -or $Path.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)
}
$root = Full $Destination
$sourceRoot = Full $Source
$stateRoot = Full (Join-Path $env:LOCALAPPDATA 'CySynck')
if ($root.Length -le 3 -or $root -eq (Full $env:USERPROFILE) -or (Within $stateRoot $root)) { throw 'Choose a separate application installation directory.' }
function Child([string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.([\\/]|$)') { throw 'Invalid installation manifest path.' }
    $path = [IO.Path]::GetFullPath((Join-Path $root $Relative))
    if (!(Within $root $path) -or $path -eq $root) { throw 'Installation path escaped the destination.' }
    $parent = $path
    while ($parent) {
        if (Test-Path -LiteralPath $parent) {
            if (((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Linked installation paths are unsupported.' }
        }
        $parent = [IO.Path]::GetDirectoryName($parent)
    }
    $path
}
function Hash([string]$Path) {
    $algorithm=[Security.Cryptography.SHA256]::Create()
    $stream=$null
    try {
        $stream=[IO.File]::OpenRead($Path)
        [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','')
    } finally { if ($stream) { $stream.Dispose() }; $algorithm.Dispose() }
}
function Remove-Empty([string[]]$Directories) {
    foreach ($directory in ($Directories | Select-Object -Unique | Sort-Object Length -Descending)) {
        if (!(Within $root $directory)) { throw 'Cleanup path escaped the installation.' }
        try { [IO.Directory]::Delete($directory,$false) } catch [IO.IOException] {} catch [UnauthorizedAccessException] {}
    }
}
$manifestPath = Join-Path $root '.cysynck-install.json'
$old = $null
$oldText = $null
if (Test-Path -LiteralPath $manifestPath) {
    $oldText = [IO.File]::ReadAllText($manifestPath)
    $old = $oldText | ConvertFrom-Json
    if ($old.product -ne 'CySynck' -or $old.schema -ne 1 -or (Full $old.root) -ne $root) { throw 'This directory has an unrelated installation manifest.' }
}
if ($Action -eq 'Status') {
    [pscustomobject]@{ installed = ($null -ne $old); destination = $root; version = $(if ($old) { $old.version } else { $null }) } | ConvertTo-Json
    return
}
$oldFiles = @{}
if ($old) { foreach ($file in $old.files) { $null = Child $file.path; $oldFiles[$file.path] = $file } }
function Check-Unlocked([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    try { $stream = [IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None); $stream.Dispose() }
    catch { throw "Quit CySynck and any application using its SDK before continuing: $Path" }
}
function Shortcut-Paths {
    $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'CySynck'
    @((Join-Path $menu 'CySynck.lnk'), (Join-Path $menu 'Uninstall CySynck.lnk'))
}
function Remove-OwnedShortcuts {
    $shell = New-Object -ComObject WScript.Shell
    $links = @(Shortcut-Paths)
    $targets = @((Child 'bin\cysynck-desktop.exe'), (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'))
    for ($i=0; $i -lt $links.Count; $i++) {
        if (!(Test-Path -LiteralPath $links[$i])) { continue }
        $link = $shell.CreateShortcut($links[$i])
        if ($link.TargetPath -eq $targets[$i] -and $link.Description -eq ('CySynck installation / '+$root)) { Remove-Item -LiteralPath $links[$i] }
    }
    try { [IO.Directory]::Delete([IO.Path]::GetDirectoryName($links[0]),$false) } catch [IO.IOException] {}
}
if ($Action -eq 'Uninstall') {
    if (!$old) { throw 'No managed CySynck installation was found in this directory.' }
    $retained = New-Object 'System.Collections.Generic.List[object]'
    $directories = New-Object 'System.Collections.Generic.List[string]'
    foreach ($file in $old.files) {
        $path = Child $file.path
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            if ((Hash $path) -ne $file.sha256) { $retained.Add($file) } else { Check-Unlocked $path }
        }
    }
    if (!$old.portable) {
        # Cleanup is local and limited to shortcuts targeting this installation.
        $cli = Child 'bin\cysynck.exe'
        $result = & $cli desktop startup cleanup
        if ($LASTEXITCODE -ne 0) { throw 'Login entry cleanup failed; the application files were kept.' }
        $cleaned = ($result -join "`n") | ConvertFrom-Json
        if (@($cleaned.skipped).Count -ne 0) { throw 'Some login entries could not be inspected; the application files were kept.' }
    }
    if ($old.shortcuts) { Remove-OwnedShortcuts }
    foreach ($file in $old.files) {
        $path = Child $file.path
        if ((Test-Path -LiteralPath $path -PathType Leaf) -and (Hash $path) -eq $file.sha256) { Remove-Item -LiteralPath $path }
        $parent = [IO.Path]::GetDirectoryName($path)
        while ($parent -and (Within $root $parent)) { $directories.Add($parent); $parent=[IO.Path]::GetDirectoryName($parent) }
    }
    Remove-Item -LiteralPath $manifestPath
    Remove-Empty $directories.ToArray()
    [pscustomobject]@{ uninstalled = $true; destination = $root; modified_files_kept = @($retained | ForEach-Object { $_.path }); profiles_preserved = $true } | ConvertTo-Json
    return
}
if ((Within $sourceRoot $root) -or (Within $root $sourceRoot)) { throw 'Source and destination must be separate directories.' }
if ($Portable) { $NoShortcuts=$true }
if ($old -and [bool]$old.portable -ne [bool]$Portable) { throw 'Keep the same Portable mode when updating this installation.' }
foreach ($required in @('bin\cysynck.exe','bin\cysynck-desktop.exe','bin\cysynck_core.dll','share\cysynck\version.json','Install-CySynck.ps1')) {
    if (!(Test-Path -LiteralPath (Join-Path $sourceRoot $required) -PathType Leaf)) { throw "Missing package file: $required" }
}
if (!$old -and (Test-Path -LiteralPath $root) -and @(Get-ChildItem -LiteralPath $root -Force).Count -ne 0) { throw 'The initial installation directory must be empty.' }
$incoming = New-Object 'System.Collections.Generic.List[object]'
foreach ($folder in @('bin','lib','include','share')) {
    $directory = Join-Path $sourceRoot $folder
    if (!(Test-Path -LiteralPath $directory)) { continue }
    foreach ($file in (Get-ChildItem -LiteralPath $directory -File -Recurse)) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Linked package files are unsupported.' }
        $relative = $file.FullName.Substring($sourceRoot.Length+1)
        $incoming.Add([pscustomobject]@{ path=$relative; sha256=(Hash $file.FullName); source=$file.FullName })
    }
}
$scriptPath = Join-Path $sourceRoot 'Install-CySynck.ps1'
$incoming.Add([pscustomobject]@{ path='Install-CySynck.ps1'; sha256=(Hash $scriptPath); source=$scriptPath })
$incomingNames = @{}
foreach ($file in $incoming) {
    $incomingNames[$file.path] = $true
    $path = Child $file.path
    if (Test-Path -LiteralPath $path) {
        if (!$oldFiles.ContainsKey($file.path)) { throw "An unmanaged file occupies $path" }
        if ((Hash $path) -ne $oldFiles[$file.path].sha256) { throw "A managed file was modified; keep a copy before updating: $path" }
        Check-Unlocked $path
    }
}
$obsolete = @($oldFiles.Keys | Where-Object { !$incomingNames.ContainsKey($_) })
foreach ($relative in $obsolete) {
    $path = Child $relative
    if (Test-Path -LiteralPath $path) {
        if ((Hash $path) -ne $oldFiles[$relative].sha256) { throw "A removed package file was modified: $path" }
        Check-Unlocked $path
    }
}
$null = [IO.Directory]::CreateDirectory($root)
$backupRoot = Child ('.cysynck-backup-'+[Guid]::NewGuid().ToString('N'))
$changes = New-Object 'System.Collections.Generic.List[object]'
$temporary = New-Object 'System.Collections.Generic.List[string]'
$cleanup = New-Object 'System.Collections.Generic.List[string]'
try {
    foreach ($relative in @($incoming | ForEach-Object { $_.path }) + $obsolete) {
        $path = Child $relative
        $backup = Join-Path $backupRoot $relative
        $existed = Test-Path -LiteralPath $path -PathType Leaf
        if ($existed) { $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backup)); Copy-Item -LiteralPath $path -Destination $backup }
        $changes.Add([pscustomobject]@{path=$path; backup=$backup; existed=$existed})
        if (!$incomingNames.ContainsKey($relative)) { if ($existed) { Remove-Item -LiteralPath $path }; continue }
        $file = $incoming | Where-Object { $_.path -eq $relative } | Select-Object -First 1
        $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        $pending=$path+'.'+[Guid]::NewGuid().ToString('N')+'.pending'
        $temporary.Add($pending)
        Copy-Item -LiteralPath $file.source -Destination $pending
        if ((Hash $pending) -ne $file.sha256) { throw 'A copied package file failed its integrity check.' }
        Move-Item -LiteralPath $pending -Destination $path -Force
    }
    $version = (Get-Content -LiteralPath (Join-Path $sourceRoot 'share\cysynck\version.json') -Raw | ConvertFrom-Json).version
    $manifest = [ordered]@{schema=1; product='CySynck'; version=$version; root=$root; portable=[bool]$Portable; shortcuts=[bool]($old -and $old.shortcuts); files=@($incoming | Select-Object path,sha256)}
    [IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding($false)))
} catch {
    $failure=$_
    $undo=$changes.ToArray(); [Array]::Reverse($undo)
    $rollbackDirectories=New-Object 'System.Collections.Generic.List[string]'
    foreach ($change in $undo) {
        try {
            if ($change.existed) {
                Copy-Item -LiteralPath $change.backup -Destination $change.path -Force
                Remove-Item -LiteralPath $change.backup
            } elseif (Test-Path -LiteralPath $change.path) { Remove-Item -LiteralPath $change.path }
        }
        catch { Write-Warning "Rollback copy retained at $($change.backup)" }
        foreach ($start in @([IO.Path]::GetDirectoryName($change.path),[IO.Path]::GetDirectoryName($change.backup))) {
            $directory=$start
            while ($directory -and (Within $root $directory)) { $rollbackDirectories.Add($directory); $directory=[IO.Path]::GetDirectoryName($directory) }
        }
    }
    if ($oldText) { [IO.File]::WriteAllText($manifestPath,$oldText,(New-Object Text.UTF8Encoding($false))) }
    elseif (Test-Path -LiteralPath $manifestPath) { Remove-Item -LiteralPath $manifestPath }
    Remove-Empty $rollbackDirectories.ToArray()
    throw $failure
} finally {
    foreach ($path in $temporary) { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path } }
}
# The file transaction succeeded. Remove only its explicit backup files and empty directories.
foreach ($change in $changes) {
    if (Test-Path -LiteralPath $change.backup) { Remove-Item -LiteralPath $change.backup }
    $directory=[IO.Path]::GetDirectoryName($change.backup)
    while ($directory -and (Within $backupRoot $directory)) { $cleanup.Add($directory); $directory=[IO.Path]::GetDirectoryName($directory) }
}
Remove-Empty $cleanup.ToArray()
if (!$NoShortcuts) {
    try {
        $shell=New-Object -ComObject WScript.Shell
        $links=@(Shortcut-Paths)
        foreach ($path in $links) {
            if ((Test-Path -LiteralPath $path) -and $shell.CreateShortcut($path).Description -ne ('CySynck installation / '+$root)) { throw 'An unrelated Start menu shortcut already exists.' }
        }
        $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($links[0]))
        $target=Child 'bin\cysynck-desktop.exe'
        $link=$shell.CreateShortcut($links[0]); $link.TargetPath=$target; $link.Arguments='--foreground'; $link.Description='CySynck installation / '+$root; $link.Save()
        $powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $link=$shell.CreateShortcut($links[1]); $link.TargetPath=$powershell
        $link.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Child 'Install-CySynck.ps1')+'" -Action Uninstall -Destination "'+$root+'"'
        $link.Description='CySynck installation / '+$root; $link.Save()
        $manifest.shortcuts=$true
        [IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding($false)))
    } catch {
        Write-Warning "Installed; Start menu shortcuts could not all be created: $_"
        foreach ($path in @(Shortcut-Paths)) {
            if (Test-Path -LiteralPath $path) {
                try { if ($shell.CreateShortcut($path).Description -eq ('CySynck installation / '+$root)) { $manifest.shortcuts=$true } } catch {}
            }
        }
        [IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding($false)))
    }
}
[pscustomobject]@{installed=$true; destination=$root; version=$version; shortcuts=$manifest.shortcuts; startup_changed=$false} | ConvertTo-Json
