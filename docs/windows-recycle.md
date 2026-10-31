# Windows Recycle Bin integration (Synchrorep 0.9)

Synchrorep uses the Windows Shell IFileOperation COM API with
FOFX_RECYCLEONDELETE and FOF_ALLOWUNDO for deletions on eligible local
fixed drives. The engine first creates the existing SHA-256 verified
recovery archive when **Preserve old versions** is enabled.

## Rules

1. Each synchronization profile has **Use Windows Recycle Bin (local)**,
   enabled by default. It is stored in SYNCHROREP-PROFILES3.
2. Only local **fixed drive-letter volumes** qualify for native recycling.
   Windows network shares, UNC paths, WSL UNC, FTP and SFTP do **not**
   qualify and continue using their existing deletion behavior. When
   archive protection is enabled, the old files are saved before removal.
3. When a local drive qualifies, the shell request uses IFileOperation.
   If the shell operation fails or is aborted, Synchrorep reports an
   error. There is **no programmatic fallback** to permanent deletion.
4. For all endpoints, a disabled recycle setting means regular removal.
   The existing recovery archive setting is independent.
5. No native recycle operation is attempted in Linux builds; Linux uses
   permanent filesystem deletion after backup, as before.

## Windows-specific safety limits

The Shell API can honor Windows user/system Recycle Bin policy, and its
operation may display a warning where permanent deletion would otherwise
be required. Using **Preserve old versions** remains strongly advised:
Windows configuration, recycle-bin size and policy can vary.

Windows/WSL/network tests are not yet executed on an actual Windows 11
workstation. The Windows code is cross-compiled with MinGW POSIX, and
the engine dispatch is tested with an injectable simulated recycle
endpoint. A native Windows GUI/Explorer smoke test is still required.

The UI reports the number of items recycled separately from total deleted
items and saved recovery copies. Every recycle attempt also receives an
event in the JSONL run log.

## Profile migration

- SYNCHROREP-PROFILES1 -> V3: preserve_changes=true, recycle_local_deletions=true.
- SYNCHROREP-PROFILES2 -> V3: existing preserve_changes is retained;
  recycle_local_deletions=true.
- V3 stores and validates both settings, including explicit opt-out.

## Automated tests

The regression suite uses an Endpoint wrapping a real temporary local
directory and simulating native recycle through a separate directory.
It tests recycle enabled, disabled, failing, and the absence of automatic
permanent deletion after a failed recycle request. It also validates that
the previous journal remains for explicit recovery.
