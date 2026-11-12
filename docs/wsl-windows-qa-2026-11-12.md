# WSL and native Windows synchronization QA — 2026-11-12

Branch: `feat/wsl-recovery-exclusions`. Tested through commit `7975924`.

## Test environments

- Windows 11 host running the native GCC/UCRT64 build of the local Synchrorep engine and CLI.
- Ubuntu 24.04 on WSL2, accessed from Windows through `\\\\wsl.localhost\\Ubuntu-24.04`.
- Temporary, isolated synchronization directories; no user data involved.

The native test executable was built from the repository's local/engine sources with `remote_stub.cpp`, **without libcurl and SQLite support**. It is not the release MSI.

## Results

| Scenario | Result |
| --- | --- |
| Native Windows → WSL initial mirror and Unicode file names/content | PASS |
| Native Windows → WSL update, deletion, no-op second run | PASS |
| Native WSL → Windows mirror with 75 files and 16 MiB binary | PASS |
| Native WSL → Windows SHA-256, update, deletion, no-op | PASS |
| Native Windows → WSL forced termination during a 256 MiB transfer | PASS (interrupted journal and temporary artifact reproduced) |
| Restart from interrupted journal, SHA-256 equality, journal/artifact cleanup | PASS |
| Native Windows exclusion patterns `*.tmp`, `cache/**`, `**/*.log` | PASS |
| Excluded destination content retained in mirror mode | PASS |
| Exclusion patterns and recovery setting persisted in profile V7 | PASS |
| Excluded WSL directory containing a symbolic link | PASS after scanner pruning fix |
| Linux/WSL CTest suites (including new crash and excluded-symlink regression cases) | 15/15 PASS |

### Reproduced defects and fixes

1. Windows `ReplaceFileW` fails when overwriting some files on the WSL UNC filesystem. The `MoveFileExW` replacement fallback succeeded in the native UNC tests.
2. A hard interruption left `.synchrorep-write-<id>.tmp` and an unfinished journal, causing the next scan to block. Recovery preparation now validates journal endpoint identities, removes only matching unfinished transfer artifacts, and runs *before* the CLI/UI preview for recovery executions. Read-only previews do not initiate cleanup.
3. A symlink inside a directory excluded by `cache/**` still caused the full filesystem scan to fail, because exclusions were applied after traversal. Local scans now prune excluded entries and directories before reading their metadata; non-excluded symlinks remain rejected.

### Performance observation

The first native WSL exclusion/symlink regression run took about 53 seconds end-to-end (including compiler invocation). A subsequent execution of the same native test executable took **0.16 seconds**; a persistent scanner slowdown was not reproduced.

## Still required for release

- Build and validate the full Windows GUI, FTP/SFTP dependencies, SQLite history, and MSI package.
- Run native GUI automation for editing exclusions and the default recovery checkbox.
- Validate the installer DLL/runtime closure on a clean Windows profile.

The branch has **not** been merged or released.
