# Synchrorep behavioral compatibility ledger

Reference: [sebk69/Synchrorep](https://github.com/sebk69/Synchrorep)
(2009–2019, GPL-3.0).

This is an incremental Windows port, NOT a completed copy of the original.
The original license and attribution must remain on any transplanted files.

## Original states from `tc_files_binomial.h`

| Original `_files_compare` | Current behavior |
| --- | --- |
| `_files_not_modified` | `Action::None` |
| `_from_modified` | `Action::CopyLeftToRight` |
| `_to_modified` | `Action::CopyRightToLeft` |
| `_from_deleted` | `Action::DeleteRight` when deletion propagation enabled |
| `_to_deleted` | `Action::DeleteLeft` when enabled |
| `_both_deleted` | `Action::None`, both removed from saved snapshot |
| `_both_modified_from_recent` | `Conflict`, with optional Newest / Oldest |
| `_both_modified_to_recent` | `Conflict`, with optional Newest / Oldest |
| `_both_modified_equal` | `Action::None` only if content actually matches |
| `_comparison_error` | Throws, run aborted without updating state |

This port deliberately treats same timestamps but different content as
**conflicting**, rather than silently treating them as equal.

## Original options (ac_behavior.h)

| Preference | Status |
| --- | --- |
| Ask / ignore / stop on error | Stop on error; other modes pending |
| Ask / ignore / stop / newest / oldest conflict | Decisions supported in core; UI only Ask |
| Alert before deletion | Present in Windows prototype |
| Move deleted/replaced files to trash | Pending, no equivalent remote trash defined |
| Ask questions at the end | Pending |
| MD5 comparison levels | Pending; byte-for-byte on bilateral change |
| Logging none/errors/actions | Pending |
| Synchronization and copy mode | Sync available; copy mode pending |
| Group synchronizations | Pending |
| Per-directory cards, preferences, logs and progress | Pending UI parity |

## Endpoint abstraction

`Endpoint::scan/read/write/make_directory/remove` is independent of the
comparison engine. It uses POSIX-style relative names regardless of backend.
The backend must enumerate every item and **fail closed** on incomplete listings.

| Endpoint | Status |
| --- | --- |
| Local Windows filesystem | Implemented with native C++ filesystem |
| WSL | Maps `wsl://Ubuntu/home/user` to `\\wsl.localhost\Ubuntu\home\user`, Windows-only; runtime test pending |
| FTP | Scheme recognized, disabled until implementation and integration tests |
| SFTP | Scheme recognized, disabled until implementation and integration tests |

Do not store usernames/passwords in history files or repository configuration.
Use Windows Credential Manager, SSH host-key verification, and a documented
trust model when FTP/SFTP is implemented.

## State and safety

- Format `SYNCHROREP2` is a **new** text snapshot; original SQLite migration
  and compatibility are not implemented.
- State is bound to ordered endpoint identities. Reusing it with different
  endpoints is an error.
- In the Windows UI each ordered pair receives an isolated history file.
- Scan errors, symbolic links, nested roots and incompatible history block a run.
- By default any conflict stops the run before files are copied or removed.
- With Ignore, unresolved conflicts remain unresolved in history.
- File copies are staged in the destination directory and promoted by rename.
- A system crash or transfer error can leave a partially-applied batch; there
  is not yet a transactional journal. **Test with disposable data only.**
- Deletions use permanent removal (no recycle bin yet). Confirmation is shown
  in the Windows prototype; CLI usage requires the caller's care.

## Remaining port tasks

1. Regression fixtures driven from original `execute.cpp` and
   `tc_files_binomial.cpp`; exact semantics and option matrix.
2. Remote FTP & SFTP endpoints, metadata normalization, transfer staging,
   reconnection, retry and integration tests.
3. Faithful recreation of GTK configuration cards, conflicts, logs, progress
   and expert mode in a native Windows frontend.
4. Windows 11 modern context menu (IExplorerCommand), installer MSI, signing.
5. Journaling, recovery after interruptions and SQLite history migration.
