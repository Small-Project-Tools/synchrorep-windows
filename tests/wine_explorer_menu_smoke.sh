#!/usr/bin/env bash
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
# Verify machine-scoped classic Explorer commands in the actually installed MSI.
set -euo pipefail
: "${WINEPREFIX:?Wine prefix required}"
mode="${1:?usage: wine_explorer_menu_smoke.sh installed|removed}"
base='HKLM\Software\Classes'
for relative in 'Directory\shell\Synchrorep' 'Directory\Background\shell\Synchrorep' 'Drive\shell\Synchrorep'; do
  printf -v key '%s\\%s' "$base" "$relative"
  case "$mode" in
    installed)
      wine reg query "$key" /ve | grep -F 'Synchronize with Synchrorep' >/dev/null
      wine reg query "$key" /v Icon | grep -i 'synchrorep.exe' >/dev/null
      output="$(wine reg query "$key\command" /ve)"
      printf '%s\n' "$output" | grep -i 'synchrorep.exe' >/dev/null
      if [[ "$relative" == *Background* ]]; then
        printf '%s\n' "$output" | grep -F '%V' >/dev/null
      else
        printf '%s\n' "$output" | grep -F '%1' >/dev/null
      fi
      echo "PASS Explorer shell registry: $relative"
      ;;
    removed)
      if wine reg query "$key" /ve >/dev/null 2>&1;then
        echo "FAIL Explorer shell key not removed: $key" >&2
        exit 1
      fi
      echo "PASS Explorer shell removed: $relative"
      ;;
    *) echo "Invalid test mode $mode" >&2;exit 2;;
  esac
done
