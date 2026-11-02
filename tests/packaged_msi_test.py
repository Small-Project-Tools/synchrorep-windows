#!/usr/bin/env python3
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the actual MSI payload, not merely its MSI File table."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from stage_windows_runtime import imports, SYSTEM_DLLS

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def validate(build, version):
    msi = build / f"synchrorep-windows-{version}-x64.msi"
    if not msi.is_file():
        raise RuntimeError(f"Missing MSI: {msi}")
    manifest = build / "windows-dlls.txt"
    if not manifest.is_file():
        raise RuntimeError("Missing Windows DLL manifest")
    dlls = [name.strip() for name in manifest.read_text().splitlines() if name.strip()]
    if len(set(dlls)) != len(dlls):
        raise RuntimeError("Duplicate DLLs in manifest")
    required_dlls = {"libcurl-4.dll", "libssh2-1.dll", "libsqlite3-0.dll"}
    if not required_dlls.issubset(set(dlls)):
        raise RuntimeError(f"Required DLL absent from manifest: {sorted(required_dlls-set(dlls))}")
    required = ["synchrorep.exe", "SynchrorepExplorer.dll", *dlls]
    with tempfile.TemporaryDirectory(prefix="synchrorep-msi-") as directory:
        root = Path(directory)
        subprocess.run(["msiextract", "-C", str(root), str(msi.resolve())], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        installed = {}
        for name in required:
            matches = [p for p in root.rglob("*") if p.is_file() and p.name.lower() == name.lower()]
            if len(matches) != 1:
                raise RuntimeError(f"MSI payload must include exactly one {name}; found {len(matches)}")
            source = build / name
            if not source.is_file() or not source.stat().st_size:
                raise RuntimeError(f"Missing build dependency: {source}")
            if digest(matches[0]) != digest(source):
                raise RuntimeError(f"MSI payload hash mismatch for {name}")
            installed[name.lower()] = matches[0]
        # Validate the complete transitive PE import graph of the EXTRACTED
        # installer, not the build directory or only a manifest of DLL names.
        pending = ["synchrorep.exe", "SynchrorepExplorer.dll"]
        checked = set()
        while pending:
            name = pending.pop().lower()
            if name in checked:
                continue
            checked.add(name)
            binary = installed[name]
            for dependency in imports(binary):
                if dependency in SYSTEM_DLLS:
                    continue
                if dependency not in installed:
                    raise RuntimeError(f"Unresolved MSI runtime import: {name} -> {dependency}")
                pending.append(dependency)
        exe_parent = installed["synchrorep.exe"].parent
        misplaced = [name for name, path in installed.items() if path.parent != exe_parent]
        if misplaced:
            raise RuntimeError(f"Runtime files outside executable folder: {misplaced}")
        print(f"Verified extracted MSI: executable, Explorer extension, {len(dlls)} DLLs; SHA-256 identical; PE dependency closure complete")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--version", required=True)
    options = parser.parse_args()
    validate(options.build, options.version)

if __name__ == "__main__":
    main()
