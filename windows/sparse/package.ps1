param(
    [Parameter(Mandatory=$true)][string]$ApplicationDirectory,
    [string]$Output = "Synchrorep.Windows.0.12.3.sparse.msix"
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $ApplicationDirectory).Path
$manifestDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($name in @('synchrorep.exe', 'SynchrorepExplorer.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $name))) {
        throw "Missing native Windows application file: $name"
    }
}
if (-not (Get-Command MakeAppx.exe -ErrorAction SilentlyContinue)) {
    throw 'Windows SDK MakeAppx.exe is required'
}
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("synchrorep-sparse-" + [Guid]::NewGuid())
try {
    New-Item -ItemType Directory -Path (Join-Path $stage 'Assets') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $manifestDirectory 'AppxManifest.xml') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $manifestDirectory 'Assets/Logo44.png') -Destination (Join-Path $stage 'Assets')
    Copy-Item -LiteralPath (Join-Path $manifestDirectory 'Assets/Logo150.png') -Destination (Join-Path $stage 'Assets')
    Copy-Item -LiteralPath (Join-Path $manifestDirectory 'Assets/StoreLogo.png') -Destination (Join-Path $stage 'Assets')
    & MakeAppx.exe pack /d $stage /p $Output /o
    if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed: $LASTEXITCODE" }
    Write-Output "UNSIGNED sparse MSIX: $Output"
    Write-Output 'Sign it with a trusted certificate matching the manifest Publisher before registering.'
    Write-Output 'Install with Add-AppxPackage -Path <signed.msix> -ExternalLocation <installed-application-folder>.'
}
finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
