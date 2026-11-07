param([Parameter(Mandatory=$true)][string]$Package)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$packageRoot=[IO.Path]::GetFullPath($Package)
$root=Join-Path $workspace ('.local\deployment-test-data\'+[Guid]::NewGuid().ToString('N'))
if (!$root.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Test target escaped the workspace.' }
$destination=Join-Path $root 'Installed application'
$installer=Join-Path $packageRoot 'Install-CySynck.ps1'
$checks=0
function Check([bool]$Value,[string]$Message) { if (!$Value) { throw $Message }; $script:checks++ }
function Rejects([scriptblock]$Work,[string]$Message) { try { & $Work } catch { $script:checks++; return }; throw $Message }
$result=(& $installer -Source $packageRoot -Destination $destination -Portable) | ConvertFrom-Json
Check $result.installed 'The package installs into a new explicit directory.'
Check (!$result.startup_changed -and !$result.shortcuts) 'Portable tests must not register startup or Start menu entries.'
$cli=Join-Path $destination 'bin\cysynck.exe'
$state=Join-Path $root 'Preserved profile'
$overview=(& $cli --state $state overview) | ConvertFrom-Json
Check ($LASTEXITCODE -eq 0 -and $overview.version -eq $result.version) 'The installed CLI and core run together.'
$registry=Join-Path $state 'registry.json'
$stateBefore=[Convert]::ToBase64String([IO.File]::ReadAllBytes($registry))
$help=(& $cli docs backups --language fr --section verifier-et-restaurer --print) | ConvertFrom-Json
Check ($LASTEXITCODE -eq 0 -and !$help.opened -and $help.url -match '^file:///.*#/fr/backups/verifier-et-restaurer$') 'Installed contextual help resolves without launching a browser.'
Check (Test-Path -LiteralPath (Join-Path $destination 'Documentation\DocData\data.js')) 'The installed reader includes its local content provider.'
Check (Test-Path -LiteralPath (Join-Path $destination 'CySynck-Documentation.zip')) 'The complete offline help archive is installed.'
$sentinel=Join-Path $destination 'personal-note.txt'
[IO.File]::WriteAllText($sentinel,'Unmanaged data must survive.')
# Model an owned help file from the previous Markdown-based distribution.
$legacyHelp=Join-Path $destination 'share\cysynck\docs\legacy-help.md'
$null=[IO.Directory]::CreateDirectory((Split-Path -Parent $legacyHelp))
[IO.File]::WriteAllText($legacyHelp,'Previous packaged documentation.')
$legacyOriginal=[IO.File]::ReadAllBytes($legacyHelp)
$installedManifest=Join-Path $destination '.cysynck-install.json'
$legacyManifest=Get-Content -LiteralPath $installedManifest -Raw | ConvertFrom-Json
$legacyAlgorithm=[Security.Cryptography.SHA256]::Create()
try { $legacyHash=[BitConverter]::ToString($legacyAlgorithm.ComputeHash($legacyOriginal)).Replace('-','').ToLowerInvariant() }
finally { $legacyAlgorithm.Dispose() }
$legacyManifest.files=@($legacyManifest.files)+@{path='share/cysynck/docs/legacy-help.md';sha256=$legacyHash}
[IO.File]::WriteAllText($installedManifest,($legacyManifest | ConvertTo-Json -Depth 8))
[IO.File]::AppendAllText($legacyHelp,"`nUser change.`n")
Rejects { & $installer -Source $packageRoot -Destination $destination -Portable | Out-Null } 'Migration must preserve a modified legacy help file by refusing its removal.'
Check ([IO.File]::ReadAllText($legacyHelp).Contains('User change.')) 'A rejected migration preserves the user change.'
[IO.File]::WriteAllBytes($legacyHelp,$legacyOriginal)
$updated=(& $installer -Source $packageRoot -Destination $destination -Portable) | ConvertFrom-Json
Check $updated.installed 'A managed package can update in place without deleting user files.'
Check (!(Test-Path -LiteralPath $legacyHelp)) 'Migration removes an unchanged legacy help file after its replacement is installed.'
$readme=Join-Path $destination 'README.md'
$original=[IO.File]::ReadAllBytes($readme)
[IO.File]::AppendAllText($readme,"`nUser note.`n")
Rejects { & $installer -Source $packageRoot -Destination $destination -Portable | Out-Null } 'Updates must preserve modified managed files.'
Check ([IO.File]::ReadAllText($sentinel) -eq 'Unmanaged data must survive.') 'The failed update must preserve unrelated files.'
[IO.File]::WriteAllBytes($readme,$original)
$dll=Join-Path $destination 'bin\cysynck_core.dll'
$lock=[IO.File]::Open($dll,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try { Rejects { & $installer -Source $packageRoot -Destination $destination -Portable | Out-Null } 'Updates must reject a locked SDK binary before replacing files.' }
finally { $lock.Dispose() }
$fixture=Join-Path $root 'Incomplete update fixture'
Copy-Item -LiteralPath $packageRoot -Destination $fixture -Recurse
$originalCli=[IO.File]::ReadAllBytes($cli)
# The fake executable is copied but never launched. A later directory collision
# must roll it back to the original executable before this test runs it again.
[IO.File]::WriteAllText((Join-Path $fixture 'bin\cysynck.exe'),'Rollback fixture, not an executable.')
[IO.File]::WriteAllText((Join-Path $fixture 'bin\deployment-marker.txt'),'New file to roll back.')
$newDirectory=Join-Path $fixture 'share\cysynck\new-feature'
$null=[IO.Directory]::CreateDirectory($newDirectory)
[IO.File]::WriteAllText((Join-Path $newDirectory 'info.txt'),'The copy must fail at this late step.')
$blocker=Join-Path $destination 'share\cysynck\new-feature'
[IO.File]::WriteAllText($blocker,'Existing untracked file blocking directory creation.')
Rejects { & $installer -Source $fixture -Destination $destination -Portable | Out-Null } 'An error after copying files must roll the update back.'
Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes($cli)) -eq [Convert]::ToBase64String($originalCli)) 'Rollback restores the previous executable exactly.'
Check (!(Test-Path -LiteralPath (Join-Path $destination 'bin\deployment-marker.txt'))) 'Rollback removes newly copied files.'
Check ([IO.File]::ReadAllText($blocker) -eq 'Existing untracked file blocking directory creation.') 'Rollback preserves the unrelated blocking file.'
Check (@(Get-ChildItem -LiteralPath $destination -Directory -Filter '.cysynck-backup-*').Count -eq 0) 'Successful rollback cleans its own backup copies.'
$restored=(& $cli --state $state overview) | ConvertFrom-Json
Check ($LASTEXITCODE -eq 0 -and $restored.version -eq $result.version) 'The previous installation remains runnable after rollback.'
[IO.File]::AppendAllText($readme,"`nKeep this modified documentation.`n")
$uninstalled=(& (Join-Path $destination 'Install-CySynck.ps1') -Action Uninstall -Destination $destination) | ConvertFrom-Json
Check $uninstalled.uninstalled 'The installed deployment script can uninstall its package.'
Check (Test-Path -LiteralPath $sentinel) 'Uninstall must keep untracked files.'
Check (Test-Path -LiteralPath $readme) 'Uninstall must keep modified tracked files.'
Check (!(Test-Path -LiteralPath $dll)) 'Uninstall removes unmodified application binaries.'
Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes($registry)) -eq $stateBefore) 'Uninstall must not change profile data.'
$foreign=Join-Path $root 'Unmanaged directory'
$null=[IO.Directory]::CreateDirectory($foreign)
[IO.File]::WriteAllText((Join-Path $foreign 'note.txt'),'Existing data.')
Rejects { & $installer -Source $packageRoot -Destination $foreign -Portable | Out-Null } 'An initial installation cannot overwrite an unmanaged directory.'
$bad=Join-Path $root 'Invalid manifest'
$null=[IO.Directory]::CreateDirectory($bad)
$manifest=@{schema=1;product='CySynck';version='test';root=$bad;portable=$true;shortcuts=$false;files=@(@{path='..\Unmanaged directory\note.txt';sha256='invalid'})} | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $bad '.cysynck-install.json'),$manifest)
Rejects { & $installer -Action Uninstall -Destination $bad | Out-Null } 'A manifest cannot escape its installation directory.'
Check ([IO.File]::ReadAllText((Join-Path $foreign 'note.txt')) -eq 'Existing data.') 'Rejected manifests leave files outside their destination intact.'
Write-Output "PASS $checks deployment checks; fixtures: $root"
