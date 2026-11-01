#!/usr/bin/env bash
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
# Real GUI regression: context-menu invocation opens folder picker directly.
# Cancellation must not save a profile or synchronize anything.
set -euo pipefail
: "${WINEPREFIX:?Wine prefix must be configured}"
: "${SYNCHROREP_EXE:?Path to installed Synchrorep required}"
test -f "$SYNCHROREP_EXE"
source_dir="$(mktemp -d "${TMPDIR:-/tmp}/synchrorep-explorer-picker.XXXXXX")"
process=""
cleanup(){
 if [[ -n "$process" ]];then kill "$process" 2>/dev/null || true;wait "$process" 2>/dev/null || true;fi
 rm -rf "$source_dir"
}
trap cleanup EXIT
printf 'probe data\n' > "$source_dir/smoke-source.txt"
win_source="$(winepath -w "$source_dir" | tr -d '\r')"
wine "$SYNCHROREP_EXE" "$win_source" > "${TMPDIR:-/tmp}/synchrorep-picker-smoke.log" 2>&1 &
process=$!
window=""
for _ in $(seq 1 25); do
  window="$(xdotool search --onlyvisible --name '^Choose the folder to synchronize with' 2>/dev/null | head -n 1 || true)"
  [[ -n "$window" ]] && break
  if ! kill -0 "$process" 2>/dev/null;then
    echo "Synchrorep exited before destination picker" >&2
    cat "${TMPDIR:-/tmp}/synchrorep-picker-smoke.log" >&2
    exit 1
  fi
  sleep 1
done
if [[ -z "$window" ]];then
 echo "Explorer invocation never opened a destination folder dialog" >&2
 cat "${TMPDIR:-/tmp}/synchrorep-picker-smoke.log" >&2
 exit 1
fi
echo "PASS: Explorer invocation displayed the destination folder picker"
# No desktop window manager is necessary to send Escape to the popup.
xdotool key --window "$window" Escape >/dev/null 2>&1
for _ in $(seq 1 12); do
 if ! kill -0 "$process" 2>/dev/null;then break;fi
 sleep 1
done
if kill -0 "$process" 2>/dev/null;then
 echo "Destination picker cancellation left Synchrorep running" >&2
 exit 1
fi
wait "$process" 2>/dev/null || true
process=""
shopt -s nullglob
for profiles in "$WINEPREFIX"/drive_c/users/*/AppData/Local/Synchrorep/profiles.dat;do
 if grep -Fq "$win_source" "$profiles";then
  echo "Cancel unexpectedly saved an Explorer synchronization profile" >&2
  exit 1
 fi
done
echo "PASS: cancelling the picker exited without creating a profile"
