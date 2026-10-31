#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
WORK="$(mktemp -d)"
export SYNCHROREP_TEST_FTP_ROOT="$WORK/ftp"
export SYNCHROREP_TEST_SFTP_ROOT="$WORK/sftp"
export SYNCHROREP_TEST_KNOWN_HOSTS="$WORK/known_hosts"
mkdir -p "$SYNCHROREP_TEST_FTP_ROOT" "$SYNCHROREP_TEST_SFTP_ROOT"
ftp_pid=''
sftp_pid=''
cleanup() {
  if [[ -n "$ftp_pid" ]]; then kill "$ftp_pid" 2>/dev/null || true; fi
  if [[ -n "$sftp_pid" ]]; then kill "$sftp_pid" 2>/dev/null || true; fi
  rm -rf "$WORK"
}
trap cleanup EXIT
python3 tests/integration/ftp_server.py > "$WORK/ftp.log" 2>&1 &
ftp_pid=$!
python3 tests/integration/sftp_server.py > "$WORK/sftp.log" 2>&1 &
sftp_pid=$!
ready=0
for i in $(seq 1 100); do
 if [[ -s "$SYNCHROREP_TEST_KNOWN_HOSTS" ]] &&
    python3 -c 'import socket; a=socket.create_connection(("127.0.0.1",2121),.4); a.close(); b=socket.create_connection(("127.0.0.1",2222),.4); b.close()' 2>/dev/null; then
   ready=1
   break
 fi
 sleep .1
done
if [[ "$ready" != 1 ]]; then
 cat "$WORK/ftp.log" "$WORK/sftp.log"
 exit 1
fi
export SYNCHROREP_REMOTE_USER="tester"
export SYNCHROREP_REMOTE_PASSWORD="secret"
export SYNCHROREP_SFTP_KNOWN_HOSTS="$SYNCHROREP_TEST_KNOWN_HOSTS"
export SYNCHROREP_TEST_REMOTE_URL="ftp://127.0.0.1:2121/"
./build-remote/remote_tests
export SYNCHROREP_TEST_REMOTE_URL="sftp://127.0.0.1:2222/"
./build-remote/remote_tests
