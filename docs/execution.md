# Execution, progress and conflict handling

Synchrorep Windows 0.5 development iteration.

The C++20 engine now provides an optional RunCallbacks argument to plan() and run().
The progress callback reports a phase, completed item count, total item count, and
current relative path. Phases are left scan, right scan, copying, deleting, left
verification, right verification, saving history, and completion.

A cancellation callback is checked before scanning and before each file operation,
and before persisting history. The Windows frontend executes plan and run in a
separate worker thread. The Cancel operation button signals cancellation. libcurl transfers and remote
listings now use CURLOPT_XFERINFOFUNCTION to abort promptly, including during
FTP and SFTP downloads/uploads. Local file hashing still checks cancellation
between files, not within the hashing loop itself.
After cancellation, already-completed copies or deletions remain on disk while
the previous history is preserved. The write-ahead recovery journal (flushed to disk before mutations) remains on
disk. A subsequent run is refused by default: the user must inspect Preview
and explicitly enable recovery (Windows checkbox or CLI --resume), at which
point both endpoints are re-scanned and compared with the last committed state.

Per-file resolution options are Keep left, Keep right, and Skip file.
A skipped conflict is deliberately NOT recorded as resolved in the snapshot.
Type clashes (regular file vs directory) cannot be forcibly overwritten through
the choice buttons, except choosing Skip. Users must resolve incompatible
file/directory types manually.

The Windows UI remains a prototype and has not undergone runtime testing on a
Windows desktop. The Win32 cross-build has been verified with MinGW; FTP/SFTP on Windows is now part of the automated MinGW build; a
Wine test uses disposable FTP/SFTP servers. Tests on a native Windows 11
machine and WSL host integration remain outstanding.

Security and reliability limitations:
- Deletions are permanent; a broad confirmation appears before running.
- Remote libcurl transfers and listings can be aborted mid-transfer.
- A local hash is interrupted only between files.
- A .journal file records the operation plan and completed paths before the
  snapshot is committed. The log is not a transactional rollback mechanism.
- Recovery requires explicit consent and verifies the journal's endpoint pair.
- Temporary uploads are cleaned up on cancellation on a best-effort basis.
- Whole-directory operations are not transactional.
- Hashing remote files currently requires full downloads and may take time.
- An active window closing while a transfer is in progress joins the worker
  thread and can remain blocked until the current local operation or remote cancellation completes.

Regression tests cover explicit Keep left / Keep right / Skip decisions,
a deletion-versus-modification conflict, cancellation during a scan, cancellation
after the first completed copy, and progress notification callbacks.

The journal is flushed with fsync on POSIX and FlushFileBuffers on Windows. This improves crash resilience but does not provide a multi-file transaction or guaranteed rollback after power loss. Unexpected temporary upload artifacts cause a fail-closed scan and require manual inspection.
