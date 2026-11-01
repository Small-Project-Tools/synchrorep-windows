# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory=$true)][string]$ExePath)
$exe = (Resolve-Path -LiteralPath $ExePath).Path
$key = 'HKCU:\Software\Classes\Directory\shell\Synchrorep'
New-Item -Path $key -Force | Out-Null
Set-Item -Path $key -Value 'Synchronize with Synchrorep'
New-Item -Path "$key\command" -Force | Out-Null
Set-Item -Path "$key\command" -Value ('"{0}" "%1"' -f $exe)
Write-Host 'Context menu installed for current user. On Windows 11 it may appear under Show more options.'
