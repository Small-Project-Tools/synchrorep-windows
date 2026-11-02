# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
param(
  [Parameter(Mandatory = $true)][string]$MsiPath
)
$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path $MsiPath).Path
if (-not (Test-Path $resolved -PathType Leaf)) { throw "MSI missing: $MsiPath" }
if ([IO.Path]::GetExtension($resolved).ToLowerInvariant() -ne '.msi') { throw 'Expected .msi' }
$fileStream = [IO.File]::OpenRead($resolved)
try {
  $header = [byte[]]::new(8)
  $readCount = $fileStream.Read($header, 0, $header.Length)
} finally { $fileStream.Dispose() }
if ($readCount -lt 8 -or -not ([Convert]::ToHexString($header[0..7]) -eq 'D0CF11E0A1B11AE1')) {
  throw 'File is not an MSI compound document'
}
$installDir = Join-Path $env:ProgramFiles 'Synchrorep'
$exe = Join-Path $installDir 'synchrorep.exe'
$explorer = Join-Path $installDir 'SynchrorepExplorer.dll'
$logDir = Join-Path $env:RUNNER_TEMP 'synchrorep-msi-logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
function Invoke-Msi([string]$arguments, [string]$label) {
  $p = Start-Process -FilePath msiexec.exe -ArgumentList $arguments -Wait -PassThru -NoNewWindow
  if ($p.ExitCode -notin @(0, 3010)) { throw "$label failed: MSI exit code $($p.ExitCode)" }
}
$installed = $false
try {
  Invoke-Msi "/i `"$resolved`" /qn /norestart /L*v `"$logDir/install.log`"" 'Install'
  $installed = $true
  foreach ($file in @($exe, $explorer, (Join-Path $installDir 'LICENSE'), (Join-Path $installDir 'THIRD_PARTY_NOTICES.txt'))) {
    if (-not (Test-Path $file -PathType Leaf)) { throw "Expected installed file missing: $file" }
  }
  foreach ($dll in @('libcurl-4.dll', 'libssh2-1.dll', 'libsqlite3-0.dll')) {
    $installedDll = Join-Path $installDir $dll
    if (-not (Test-Path -LiteralPath $installedDll -PathType Leaf)) {
      throw "Required bundled runtime DLL missing: $dll"
    }
  }
  # The MSI must install every runtime dependency beside the actual executable.
  $manifest = Join-Path (Split-Path -Parent $resolved) 'windows-dlls.txt'
  if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "MSI runtime manifest missing from distribution: $manifest"
  }
  foreach ($dll in Get-Content -LiteralPath $manifest) {
      $sourceDll = Join-Path (Split-Path -Parent $resolved) $dll
      $installedDll = Join-Path $installDir $dll
      if (-not (Test-Path -LiteralPath $installedDll -PathType Leaf)) { throw "MSI did not install runtime DLL: $dll" }
      if ((Get-FileHash -LiteralPath $sourceDll -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $installedDll -Algorithm SHA256).Hash) {
        throw "Installed runtime DLL differs from distribution: $dll"
      }
    }
  & (Join-Path $PSScriptRoot 'check_windows_runtime.ps1') -ExecutablePath $exe
  foreach ($reg in @(
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Directory\shell\Synchrorep',
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Drive\shell\Synchrorep',
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Directory\Background\shell\Synchrorep'
  )) {
    if (-not (Test-Path $reg)) { throw "Explorer context menu missing: $reg" }
  }
}
finally {
  if ($installed) {
    Invoke-Msi "/x `"$resolved`" /qn /norestart /L*v `"$logDir/uninstall.log`"" 'Uninstall'
    foreach ($file in @($exe, $explorer, (Join-Path $installDir 'libcurl-4.dll'),
        (Join-Path $installDir 'libssh2-1.dll'), (Join-Path $installDir 'libsqlite3-0.dll'))) {
      if (Test-Path -LiteralPath $file) { throw "File remains after uninstall: $file" }
    }
    foreach ($reg in @(
      'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Directory\shell\Synchrorep',
      'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Drive\shell\Synchrorep',
      'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Directory\Background\shell\Synchrorep'
    )) {
      if (Test-Path $reg) { throw "Explorer context menu remains after uninstall: $reg" }
    }
  }
}
Write-Output 'MSI install, runtime, registry and uninstall verified'
