param(
  [Parameter(Mandatory = $true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$gitlabProject = 665
$gitlabHost = 'https://git.small-project.dev'
$gitlabRepository = "$gitlabHost/pub/desktop/synchrorep-windows.git"
$workspace = (Get-Location).Path
$expectedTree = (& git -C $workspace rev-parse 'HEAD^{tree}').Trim()
if ($LASTEXITCODE -ne 0 -or $expectedTree -notmatch '^[0-9a-f]{40}$') {
  throw 'Cannot determine the checked-out GitHub source tree'
}
$tempRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$source = Join-Path $tempRoot "synchrorep-gitlab-source-$PID"
try {
  & git clone --quiet --depth 1 --branch main $gitlabRepository $source
  if ($LASTEXITCODE -ne 0) { throw 'Failed to fetch the canonical GitLab source tree' }
  $actualTree = (& git -C $source rev-parse 'HEAD^{tree}').Trim()
  $gitlabSha = (& git -C $source rev-parse HEAD).Trim()
  if ($LASTEXITCODE -ne 0) { throw 'Failed to resolve the canonical GitLab commit' }
  if ($actualTree -ne $expectedTree) {
    throw "GitLab and GitHub source trees differ (GitHub $expectedTree, GitLab $actualTree); refusing to test a potentially stale MSI"
  }
  $cmake = Get-Content -LiteralPath (Join-Path $workspace 'CMakeLists.txt') -Raw
  if ($cmake -notmatch 'project\(synchrorep_windows VERSION ([0-9]+\.[0-9]+\.[0-9]+)') {
    throw 'Cannot determine the Synchrorep version'
  }
  $version = $Matches[1]
  $pipelinesUrl = "$gitlabHost/api/v4/projects/$gitlabProject/pipelines?sha=$gitlabSha&status=success&per_page=100"
  $pipelines = Invoke-RestMethod -Method Get -Uri $pipelinesUrl
  $pipeline = $pipelines | Sort-Object id -Descending | Select-Object -First 1
  if ($null -eq $pipeline) {
    throw "No successful GitLab pipeline exists for commit $gitlabSha"
  }
  $jobs = Invoke-RestMethod -Method Get -Uri "$gitlabHost/api/v4/projects/$gitlabProject/pipelines/$($pipeline.id)/jobs?per_page=100"
  $build = $jobs | Where-Object { $_.name -eq 'windows-mingw' -and $_.status -eq 'success' } | Select-Object -First 1
  if ($null -eq $build) {
    throw "GitLab pipeline $($pipeline.id) has no successful windows-mingw job"
  }
  $artifactName = "synchrorep-windows-$version-x64.msi"
  $artifactUrl = "$gitlabHost/api/v4/projects/$gitlabProject/jobs/$($build.id)/artifacts/build-win/$artifactName"
  $target = [IO.Path]::GetFullPath($OutputPath)
  $parent = Split-Path -Parent $target
  New-Item -ItemType Directory -Path $parent -Force | Out-Null
  Invoke-WebRequest -Uri $artifactUrl -OutFile $target -MaximumRedirection 3
  $file = Get-Item -LiteralPath $target
  if ($file.Length -lt 100000) { throw "Downloaded MSI is suspiciously small: $($file.Length) bytes" }
  $stream = [IO.File]::OpenRead($target)
  try {
    $magic = [byte[]]::new(8)
    $count = $stream.Read($magic, 0, 8)
  } finally { $stream.Dispose() }
  if ($count -ne 8 -or [Convert]::ToHexString($magic) -ne 'D0CF11E0A1B11AE1') {
    throw 'Downloaded artifact does not contain an MSI header'
  }
  $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
  Write-Output "GitLab commit: $gitlabSha"
  Write-Output "Git tree: $actualTree (identical in GitHub)"
  Write-Output "GitLab pipeline: $($pipeline.id); build job: $($build.id)"
  Write-Output "MSI: $artifactName ($($file.Length) bytes), SHA256: $hash"
}
finally {
  if (Test-Path -LiteralPath $source) { Remove-Item -LiteralPath $source -Recurse -Force }
}
