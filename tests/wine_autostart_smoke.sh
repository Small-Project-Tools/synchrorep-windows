#!/usr/bin/env bash
set -euo pipefail
test -n "$WINEPREFIX"
test -n "$SYNCHROREP_EXE"
local_root="$WINEPREFIX/drive_c"
source="$local_root/synchrorep-ci-source"
target="$local_root/synchrorep-ci-target"
config="$local_root/users/root/AppData/Local/Synchrorep/profiles.dat"
state_dir="$(dirname "$config")"
key="$(printf 'file:C:/synchrorep-ci-source\nfile:C:/synchrorep-ci-target' | sha256sum | cut -d' ' -f1)"
rm -f "$state_dir/$key.state" "$state_dir/$key.state.journal" "$state_dir/logs/$key.state.jsonl"
rm -rf "$state_dir/recovery/$key.state"
mkdir -p "$state_dir"
rm -rf "$source" "$target"
mkdir -p "$source" "$target"
printf 'source-one\n' > "$source/source.txt"
printf 'target-only\n' > "$target/extra.txt"
cat > "$config" <<'EOF'
SYNCHROREP-PROFILES5
"AutoSmokeCI" "C:/synchrorep-ci-source" "C:/synchrorep-ci-target" "" 0 1 1 0 1 1
EOF
wine "$SYNCHROREP_EXE" --auto-mirror AutoSmokeCI > "$WINEPREFIX/startup-smoke.log" 2>&1 &
app=$!
trap 'kill "$app" 2>/dev/null || true; wine wineserver -k >/dev/null 2>&1 || true' EXIT
wait_condition(){
  local description="$1"
  local i
  for i in $(seq 1 "$WAIT_ATTEMPTS"); do
    if eval "$2"; then
      echo "PASS: $description"
      return 0
    fi
    sleep .15
  done
  echo "FAIL: $description" >&2
  cat "$WINEPREFIX/startup-smoke.log" >&2
  return 1
}
WAIT_ATTEMPTS=130
wait_condition "Initial source file copied" 'test -f "$target/source.txt" && cmp -s "$source/source.txt" "$target/source.txt"'
wait_condition "Extraneous target entry deleted" 'test ! -e "$target/extra.txt"'
find "$state_dir/recovery/$key.state" -type f -name extra.txt -size +0c | grep -q .
echo "PASS: Recovery archive exists for deleted target-only file"
printf 'new-file\n' > "$source/new-file.tmp"
mv "$source/new-file.tmp" "$source/new-file.txt"
wait_condition "Source creation mirrored" 'test -f "$target/new-file.txt" && cmp -s "$source/new-file.txt" "$target/new-file.txt"'
printf 'wrong-drift\n' > "$target/source.txt.tmp"
mv "$target/source.txt.tmp" "$target/source.txt"
# The configured full remote reconciliation interval is 60 seconds.
# A target-only edit does not change the local source metadata.
WAIT_ATTEMPTS=540
wait_condition "Target drift healed without touching source" 'cmp -s "$source/source.txt" "$target/source.txt"'
WAIT_ATTEMPTS=130
rm "$source/new-file.txt"
wait_condition "Source deletion mirrored" 'test ! -e "$target/new-file.txt"'
grep -q '"event":"committed"' "$state_dir/logs/$key.state.jsonl"
echo "PASS: Execution log committed"
# Target deletion is observable before the engine has finalized its journal.
# Wait for commit cleanup instead of racing the asynchronous mirror worker.
WAIT_ATTEMPTS=130
wait_condition "No pending journal after completed cycles" 'test ! -f "$state_dir/$key.state.journal"'
