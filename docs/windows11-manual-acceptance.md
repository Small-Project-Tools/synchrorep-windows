# Windows 11 x64 — manual acceptance record

This checklist is **required before declaring Windows 11 / GNOME process
equivalence**. CI on Linux, MinGW and Wine is necessary but cannot replace
native Windows 11/Explorer/WSL2 verification.

Record: Windows build `________` | Git commit `________` |
Windows 11 build `________` | tester/date `________`.

For each case record **PASS / FAIL / BLOCKED**, observed result, and an
issue number or screenshot when applicable. Use disposable directories only.
**Never** run destructive checks against personal or production data.

## Packaging and Explorer (native Windows 11)

- [ ] MSI installs with no missing DLL error; launch the installed program and
      run `synchrorep.exe --check-runtime` from its installation directory
- [ ] Verify the installed `libsqlite3-0.dll` and both FTP/SFTP runtime DLLs
- [ ] In File Explorer right-click an ordinary directory: Synchrorep is
      available (possibly under **Show more options** for the classic menu)
- [ ] Invoking the folder action opens a **folder** destination picker,
      proposes a new profile with correct defaults, and starts the first sync
- [ ] Reopening a configured source uses its saved profile without duplicating it
- [ ] A folder background and drive root expose the intended action
- [ ] Canceling the picker produces no profile and no filesystem change
- [ ] Uninstall MSI and verify context menus disappear without deleting user
      profile or sync-history data
- [ ] Upgrade from an existing installed edition preserves profiles/history

## Local two-way synchronization (disposable A and B)

- [ ] First run: A contains `alpha.txt`, B empty; B obtains identical contents
- [ ] Second run: no copies and no deletions
- [ ] Edit A with a newer mtime; B receives the new contents
- [ ] Edit B with a newer mtime; A receives the new contents
- [ ] Same-size change and optional SHA-256 setting behave per documented policy
- [ ] Both sides edited with the **same** timestamp and proven different hashes:
      conflict displayed; no silent overwrite
- [ ] Delete on A after successful history: deletion propagates only when enabled
- [ ] Preserved versions are recoverable after overwrite and deletion
- [ ] Cancellation mid-run leaves an actionable recovery journal; explicit
      resume does not silently lose either version
- [ ] Rename, Unicode filename, file with `..` in its name, nested folder and
      empty folder are handled correctly
- [ ] Unavailable source volume is rejected, not interpreted as empty

## One-way mirror (local A -> disposable B)

- [ ] Initial sync A -> B, with target-only files handled as configured
- [ ] After edits, additions and deletions in A, B updates automatically
- [ ] Target-side edits are corrected from A, never copied to A
- [ ] Newly configured empty A does not purge an existing B
- [ ] Restart/Windows login autostart resumes the intended configured mirror
- [ ] Two overlapping mirror destinations cannot be activated simultaneously

## WSL2, FTP and SFTP

- [ ] With WSL2 installed, Explorer's destination picker can navigate
      `\\wsl.localhost\<distro>\home\...` and select an existing directory
- [ ] Windows <-> WSL2 sync handles nested paths, Unicode names and timestamps
- [ ] WSL2 shut down/unmounted: no deletion wave against the available side
- [ ] FTP and SFTP each complete initial copy, update, deletion and retry after
      a connection interruption against disposable test endpoints
- [ ] SFTP rejects untrusted host keys; credentials are not written into profiles
- [ ] Permissions, locked files and network disconnects produce actionable
      errors without corrupting the destination or the history

## Sign-off

Automated pipeline URL: `________`.
Manual cases passed: `__ / __`; failures: `________`.
**Windows 11 acceptance: PASS / FAIL / BLOCKED**.

Do not claim GNOME equivalence while any mandatory scenario is FAIL or BLOCKED.
Document intentional Windows differences (SHA-256 change detection,
newest-timestamp preference, no post-copy integrity hashing) separately.
