# Synchrorep Windows

Windows port of [Synchrorep](https://github.com/sebk69/Synchrorep), originally written by Sébastien Kus (2009–2019), GPL-3.0.

## Status: initial, incomplete implementation

The current repository contains an initial **native C++20** bidirectional synchronization engine, a basic Win32 window, and a Windows Explorer **classic context menu** registration script. It is **not yet feature-equivalent** to the original GTK application.

The first engine prototype supports local folders, including Windows UNC WSL shares when they are accessible through the Windows filesystem. FTP/SFTP adapters, original UI parity, and the original SQLite state format are **not implemented yet**. **Do not run it against irreplaceable data.**

### Semantics

- Tracks both sides in a persisted snapshot; the original Synchrorep used an SQLite history. This initial port uses its own text snapshot; it does not pretend to be compatible.
- Propagates creations and one-sided changes and deletions.
- Blocks an entire run if simultaneous modifications would conflict (no silent overwrite).
- Rejects symbolic links.
- Does not yet implement original behavior options (trash, policies, per-file conflict prompts, groups, audit trail, MD5 mode).
- Avoids deleting files when no prior state exists, but cannot guarantee transactions across file systems.

### Build and test
```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Build on Windows with Visual Studio CMake or through the GitLab MinGW cross-build pipeline.

### UI and context menu

Build `synchrorep.exe`. Register the context menu for the current user:

```powershell
.\windows\install-context-menu.ps1 -ExePath .\build-win\synchrorep.exe
```

Right-click a folder and select **Synchronize with Synchrorep**. The selected folder is loaded as the left side.

### Next milestones
1. Reproduce original engine behavior exactly (capture and port regression cases from `tc_files_binomial.cpp`, `execute.cpp`, `ac_behavior.cpp`).
2. Introduce `IEndpoint` abstraction for local, WSL, FTP and SFTP, with proper remote timestamps, atomic uploads, credentials and hostname validation.
3. Recreate all original GTK screens (configuration cards, global policies, conflict resolution, logs and progress) in a modern Windows UI.
4. Native Explorer extension (Windows 11 modern menu), installer and signed binaries.

GPL-3.0 applies to the inherited design/source; preserve original copyright notices when moving original code.
