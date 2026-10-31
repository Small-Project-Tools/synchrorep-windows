# Synchrorep GNOME -> Windows: process and storage audit

Reference: https://github.com/sebk69/Synchrorep (GNOME C++ / GTK2 / GIO / SQLite).
Windows reference: 0.13.5. This is a source-code audit, not a claim that
the two applications are already behaviorally identical.

## Architecture of the GNOME implementation

- `accessors/db_access.cpp`: SQLite at `~/.synchrorep.db`, tables
  `config`, `comportements`, `files`, `prefs`, `logs`. The composite
  `(from_folder, file)` is uniquely indexed in `files`.
- `accessors/ac_config.cpp`: persisted folder pairs, groups, synchronization
  mode (synchronize/copy), configuration locking; every file has the
  previous left and right modification times and optional MD5 digests.
- `technical/tc_find_files.cpp`: enumeration sorted by relative pathname.
- `applicative/synchronization.cpp`: merge of two sorted cursors (linear
  traversal), construct pairs including missing-side entries; compare and
  execute each decision; update each file's history after successful copy.
- `technical/tc_files_binomial.cpp`: state comparisons for modified/deleted/
  unchanged/both modified, explicit tie and conflict handling; temporary
  copy followed by target replacement; remote deletion/trash handling.
- `accessors/ac_behavior.cpp`: per-configuration deletion, overwrite,
  conflict, error, logging, and MD5 preferences.
- `applicative/logging.cpp` and `accessors/ac_logs.cpp`: per-action logs,
  error context and run identifiers persisted in SQLite.
- `applicative/synchronization.cpp`: asynchronous workers, single-config
  lock, progress, cancellation, conflict/deletion prompts, deferred questions.

## Parity matrix

| Process | GNOME | Windows 0.13.5 | Next action |
| --- | --- | --- | --- |
| Storage | SQLite file history keyed by pair/path | Text state + profiles.dat | Introduce SQLite and transactional migration |
| Lookup | Indexed history by relative path | Ordered in-memory map | Benchmark indexed SQLite streaming |
| Scan | Sorted two-cursor merge | Two full snapshots and union | Measure memory and adopt streaming merge where feasible |
| Detection | Prior state + timestamps + configurable MD5 | Prior state + timestamps + optional SHA-256 | Preserve **Windows SHA-256 rules**, not legacy MD5 |
| Decision | Both modified/deleted, unilateral changes, equal content | Similar action set | Differential tests across every transition |
| Conflict | Ask/ignore/stop/newest/oldest, per-run prompts | Override + newest-date policy | Test ties, stale and skipped states |
| Copy | Temporary target then replace, restore timestamps | Temporary target then atomic replace | Failure-injection and Windows/FTP/SFTP tests |
| Deletion | Confirm/trash/remote fallback | Archive, optional recycle, fail-closed | Preserve Windows safeguards |
| Cancellation | Cancellation on scan, action and prompts | Cancellation on scans/actions | Test interruption/resumption |
| Errors | Ask/ignore/stop | Primarily stop | Implement error-policy parity after audit |
| Logging | SQLite runs/actions | JSONL action logs | Move to database without losing history |
| Scheduling | Multiple parallel configurations, config locks | Mirror monitor + lock, single active UI job | Parallel profile tests |
| Connectivity | GIO local/remote; FTP handling | Local/WSL/FTP/SFTP | Protocol behavior matrix |
| Profiles | SQLite config/behavior/prefs/groups | profiles.dat V6 | Transactional import preserving credentials |
| UI | Configuration and progress separate | Separate Win32 windows | Windows UI automation |
| Restore | Trash and remote fallback | Recovery archives + Recycle Bin | Preserve current stronger safeguards |

## User-specified differences from GNOME (do NOT port old behavior)

1. SHA-256 detects an actual modification; it is not a post-copy integrity check.
2. Hash common files at first synchronization; subsequent hashing is optional.
3. Never hash a file solely for comparison if it is only present at one side.
4. When versions diverge, newest modification date wins; exact ties with
   different proven contents require a conflict decision.
5. Never silently delete a destination because a local mount/distribution
   is unavailable.

## Implementation sequence

1. **Storage component (implemented, not yet wired):** `SqliteHistory`
   with index on pair/path, prepared statements, transactional replacement,
   and independent SQLite tests.
2. **Migration (pending):** import existing `SYNCHROREP4` state and V1–V6
   profiles without damaging originals; backup+rollback, idempotence, crash tests.
3. **Switch-over (pending):** wire SQLite to Engine and profile/log stores.
   Maintain existing recovery journals until their transactional equivalent
   has been failure-tested.
4. **Differential verification (pending):** exercise GNOME compare transitions
   and their corresponding Windows outcomes, including the intentional hash
   deviations above. Test each transport and verify scalability.
5. **Release only after MinGW compilation, Wine tests and Windows 11 WSL tests.**

GNOME source is GPLv3; preserve attribution and licensing if its implementation
is directly imported or adapted. Only the architecture and observed behaviors
are referenced in this phase.


## SQLite integration status

The engine now reads the existing `SYNCHROREP4` state when no SQLite
history for that pair exists. After a successful run, history is committed
as `<state>.sqlite` in a SQLite transaction. The old `.state` stays
untouched as a migration backup. An existing SQLite history for a
different endpoint pair is rejected. A pending recovery journal must
still be reviewed before the next write.

SQLite is a required dependency in the build; Windows CI fetches the
MSYS2 SQLite package with checksums, and stages the DLL import closure
into the MSI. This does **not** implement migration of the GNOME
`~/.synchrorep.db` configurations, behaviors, preferences or logs.
It also does not prove end-to-end parity of every GNOME process.
