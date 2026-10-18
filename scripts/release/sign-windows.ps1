# SPDX-License-Identifier: GPL-2.0-or-later
param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference = 'Stop'
if (-not $env:CONVOLVER_WINDOWS_CERT_SHA1 -or -not $env:CONVOLVER_TIMESTAMP_URL) {
    throw 'Certificate-store thumbprint and trusted timestamp URL are required'
}
if ($env:GITHUB_ACTIONS -eq 'true' -and $env:GITHUB_REF -notlike 'refs/tags/v*') {
    throw 'CI signing requires a trusted release tag'
}
if (Test-Path (Join-Path $Path 'artifact-manifest.json')) {
    throw 'Sign before sealing the final manifest'
}
$item = Get-Item -LiteralPath $Path
$files = if ($item.PSIsContainer) {
    Get-ChildItem -LiteralPath $Path -File -Recurse | Where-Object { $_.Extension -in '.exe', '.dll', '.vst3' }
} else { @($item) }
if (-not $files) { throw 'No code found to sign' }
foreach ($file in $files) {
    & signtool sign /sha1 $env:CONVOLVER_WINDOWS_CERT_SHA1 /fd SHA256 /tr $env:CONVOLVER_TIMESTAMP_URL /td SHA256 $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "Signing failed: $($file.Name)" }
    & signtool verify /pa /all /tw $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $($file.Name)" }
}
