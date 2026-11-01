# GNOME versus Windows: action/button process review

Reference sources: GNOME `synchrorep-core/interface/window_config_main.cpp`,
`window_card.cpp`, `widget_behavior.cpp`, `accessors/ac_config.cpp`, and
Windows `windows/main.cpp` (`onCommand`, `startSync`, `loadProfile`).

| GNOME action | Windows action | Verification / intentional difference |
| --- | --- | --- |
| New synchronization through GNOME configuration card | New | Reset form, conflict policy, hash, backups, deletion and auto-start defaults; no previous-profile fields retained |
| Modify selected configuration | Selecting profile then Save | Saves profile settings; no sync while editing |
| Delete selected configuration | Delete | Delete profile and its indexed file history, as GNOME removes `config` and `files`; preserve history when another profile uses the same endpoint pair |
| View logs | Open logs | GNOME displays an in-app SQLite log window; Windows opens the JSONL log folder (not functionally identical) |
| Default behavior / advanced preferences | Policy, deletion, protect/recycle, hash controls | Windows controls are per-profile rather than a GNOME default-behavior panel |
| Contextual integration toggle | Explorer menu | Windows registry/Explorer integration differs from GNOME/Nautilus by platform |
| Execute sync | Synchronize | Windows adds destructive-operation confirmation, recovery journaling and startup controls |
| Conflict prompts | Keep left / Keep right / Skip | Windows previews and records per-file overrides; GNOME supports deferred prompts and separate error policy |
| Cancel active job | Cancel operation | Windows requests cancellation and relies on recovery journal; GNOME may defer questions after scan |
| Mirror / autostart | Mirror / Start at Windows login | Windows-only extension; no GNOME equivalent |
| Recovery browsing | Open recovery | Windows-only preserved-version browser |

## Open parity differences

- GNOME has per-configuration `ask/ignore/stop` **error** policies and deferred
  conflict questions; Windows presently stops on operational exceptions.
- GNOME uses a central SQLite configuration, behavior, preference and log store.
  Windows still uses `profiles.dat` and JSONL logs. The SQLite **file history**
  now gets deleted with its profile, but storage architecture is not identical.
- Windows 11 Explorer integration, WSL2 folder picker and MSI installation
  require manual acceptance on actual Windows 11.

The table is a source-level review, **not a certification of behavioral parity**.
