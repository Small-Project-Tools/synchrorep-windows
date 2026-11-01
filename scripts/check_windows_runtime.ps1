param(
  [Parameter(Mandatory = $true)][string]$ExecutablePath,
  [ValidateRange(1, 300)][int]$TimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $ExecutablePath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
  throw "Windows runtime executable not found: $ExecutablePath"
}
# Explicitly await WIN32 subsystem processes: PowerShell's invocation operator
# may return without waiting or updating LASTEXITCODE.
$process = Start-Process -FilePath $exe -ArgumentList '--check-runtime' -WorkingDirectory (Split-Path -Parent $exe) -PassThru
try {
  if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill($true)
    throw "Windows runtime self-check timed out after $TimeoutSeconds seconds"
  }
  $exitCode = $process.ExitCode
  if ($exitCode -eq 0) {
    Write-Output "Windows runtime self-check passed: $exe"
    return
  }
  $reason = switch ($exitCode) {
    2 { 'The libcurl SSL or protocol metadata is incomplete' }
    3 { 'The libcurl runtime has no FTP or SFTP support' }
    -1073741515 { 'STATUS_DLL_NOT_FOUND: a runtime DLL is missing' }
    -1073741701 { 'STATUS_INVALID_IMAGE_FORMAT: incompatible DLL or architecture' }
    default { 'Consult CTest windows_transport_tests and Windows event logs' }
  }
  throw ("Windows runtime self-check failed: exit code {0} (0x{1:X8}). {2}" -f $exitCode, $exitCode, $reason)
}
finally {
  $process.Dispose()
}
