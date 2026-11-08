param([switch]$DisposableHost)
$ErrorActionPreference = 'Stop'
if (-not $DisposableHost -or $env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows') {
    throw 'The driver installer is restricted to an explicitly disposable Windows CI runner.'
}
$installer = Join-Path $env:RUNNER_TEMP 'cysynck-wireguard-amd64-1.1.1.msi'
Invoke-WebRequest 'https://download.wireguard.com/windows-client/wireguard-amd64-1.1.1.msi' -OutFile $installer
if ((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash -ne '7BFED60AD61B785C914B38B61555A975488E1D3EC472DBFB2FCDF498FCA75242') {
    throw 'The pinned official WireGuard installer checksum differs.'
}
$signature = Get-AuthenticodeSignature -LiteralPath $installer
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne '1B3AFA5E2A76BB51F00020002DCCADB165689C33') {
    throw 'The official WireGuard installer publisher signature differs.'
}
$process = Start-Process msiexec.exe -ArgumentList @('/i', ('"' + $installer + '"'), '/qn', '/norestart', 'DO_NOT_LAUNCH=1') -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -notin @(0, 3010)) { throw "WireGuard installation failed: $($process.ExitCode)" }
Write-Output 'Verified WireGuard 1.1.1 installer checksum and publisher; installed without launching the manager.'
