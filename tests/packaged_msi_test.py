#!/usr/bin/env python3
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the actual MSI payload, not merely its MSI File table."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

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
        for name in required:
            matches = [p for p in root.rglob("*") if p.is_file() and p.name.lower() == name.lower()]
            if len(matches) != 1:
                raise RuntimeError(f"MSI payload must include exactly one {name}; found {len(matches)}")
            source = build / name
            if not source.is_file() or not source.stat().st_size:
                raise RuntimeError(f"Missing build dependency: {source}")
            if digest(matches[0]) != digest(source):
                raise RuntimeError(f"MSI payload hash mismatch for {name}")
        print(f"Verified extracted MSI: executable, Explorer extension, {len(dlls)} DLLs; SHA-256 identical")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--version", required=True)
    options = parser.parse_args()
    validate(options.build, options.version)

if __name__ == "__main__":
    main()
