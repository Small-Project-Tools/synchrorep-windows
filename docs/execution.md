# Execution, progress and conflict handling

Synchrorep Windows 0.5 development iteration.

The C++20 engine now provides an optional RunCallbacks argument to plan() and run().
The progress callback reports a phase, completed item count, total item count, and
current relative path. Phases are left scan, right scan, copying, deleting, left
verification, right verification, saving history, and completion.

A cancellation callback is checked before scanning and before each file operation,
and before persisting history. The Windows frontend executes plan and run in a
separate worker thread. The Cancel operation button signals cancellation, but
does not forcibly interrupt a hash or FTP/SFTP transfer already in progress.
After cancellation, already-completed copies or deletions remain on disk while
the previous history is preserved. The following run will re-scan and reconcile.

Per-file resolution options are Keep left, Keep right, and Skip file.
A skipped conflict is deliberately NOT recorded as resolved in the snapshot.
Type clashes (regular file vs directory) cannot be forcibly overwritten through
the choice buttons, except choosing Skip. Users must resolve incompatible
file/directory types manually.

The Windows UI remains a prototype and has not undergone runtime testing on a
Windows desktop. The Win32 cross-build has been verified with MinGW; SSH/SFTP
on Windows with MSVC/vcpkg and WSL runtime tests remain outstanding.

Security and reliability limitations:
- Deletions are permanent; a broad confirmation appears before running.
- Cancellation is between files, not inside an ongoing transfer.
- Whole-directory operations are not transactional.
- Hashing remote files currently requires full downloads and may take time.
- An active window closing while a transfer is in progress joins the worker
  thread and may remain blocked until the current remote operation finishes.

Regression tests cover explicit Keep left / Keep right / Skip decisions,
a deletion-versus-modification conflict, cancellation during a scan, cancellation
after the first completed copy, and progress notification callbacks.
