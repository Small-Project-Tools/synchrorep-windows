# Synchrorep Windows

Native Windows port of [Synchrorep](https://github.com/sebk69/Synchrorep)
by Sébastien Kus (2009–2019). Original project GPL-3.0.

**Development status: an incomplete prototype. Do not use it on important data.**

## What's implemented

- Portable C++20 synchronization comparison engine with a clear backend API.
- Two-way propagation of creations, changes and one-sided deletions.
- Previous-snapshot conflict detection, byte comparisons on bilateral changes.
- Conflict handling policies (Ask/Ignore/Stop/Newest/Oldest) at engine level.
- Snapshot identity tied to the exact ordered pair of endpoints.
- Local endpoint (Windows path / UNC) and Windows-only `wsl://` to UNC mapping.
- Basic Win32 desktop app (two folders, Preview, Synchronize) and Explorer
  classic context menu install script.
- A Linux regression suite and a Windows MinGW cross-build.

FTP and SFTP are **not functional yet**: requesting them fails explicitly.

## Build/test

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### CLI (Linux, for tests)

```sh
./build/synchrorep /tmp/left /tmp/right /tmp/synchrorep.state --dry-run
./build/synchrorep /tmp/left /tmp/right /tmp/synchrorep.state
```

### Windows

Build with Visual Studio / CMake, or use the MinGW GitLab build artifact.
The app is linked as a Windows GUI executable.

Register Explorer's classic per-user context menu:

```powershell
.\windows\install-context-menu.ps1 -ExePath "C:\path\to\synchrorep.exe"
```

In Windows 11 the entry may appear under **Show more options**.
`wsl://Ubuntu/home/user` resolves to a WSL UNC path on Windows with WSL
installed.

## Compatibility / limits

See [the compatibility matrix](docs/compatibility.md) for the original
behavior options, missing features, implementation safety guarantees, and
remaining tasks. The original uses SQLite and offers richer UI and policies;
this prototype still uses a separate `SYNCHROREP2` text history format.

The engine stages writes before replacing target files but a batch of writes
and deletions is not transactional yet. In particular, the original trash,
confirmation dialogs per conflict, MD5 options and logs are not ported.

## License

The original is GPL-3.0. Retain original license and author attribution when
moving original code. This project continues under GPL-3.0.
