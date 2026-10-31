#!/usr/bin/env python3
"""Validate runtime DLLs in the actual compiled MSI, not only its WiX source."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--version", required=True)
args = parser.parse_args()
msi = args.build / f"synchrorep-windows-{args.version}-x64.msi"
if not msi.is_file():
    raise SystemExit(f"Missing MSI: {msi}")
output = subprocess.check_output(["msiinfo", "export", str(msi), "File"], text=True)
required = {"synchrorep.exe", "SynchrorepExplorer.dll"}
required.update((args.build / "windows-dlls.txt").read_text().splitlines())
missing = {name for name in required if name.lower() not in output.lower()}
if missing:
    raise SystemExit(f"Missing files from built MSI: {sorted(missing)}")
print(f"Validated built MSI includes EXE, Explorer extension, and {len(required)-2} runtime DLLs")
