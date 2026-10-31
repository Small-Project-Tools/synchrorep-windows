#!/usr/bin/env python3
"""Build a standalone Windows portable ZIP with all dependency DLLs.

The Windows executable is dynamically linked to curl/SSH/OpenSSL.
Distributing it alone is invalid. This package is validated against the
same dependency closure used in the MSI build.
"""
from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]


def portable_files(build: Path) -> list[Path]:
    required = [
        Path("synchrorep.exe"),
        Path("SynchrorepExplorer.dll"),
        Path("THIRD_PARTY_NOTICES.txt"),
    ]
    manifest = build / "windows-dlls.txt"
    if not manifest.is_file():
        raise FileNotFoundError(manifest)
    dlls = manifest.read_text(encoding="utf-8").splitlines()
    if not dlls or "libcurl-4.dll" not in dlls or "libssh2-1.dll" not in dlls:
        raise ValueError("The complete FTP/SFTP runtime must be included")
    if len(set(dlls)) != len(dlls):
        raise ValueError("Duplicate runtime dependencies")
    for dll in dlls:
        if not re.fullmatch(r"[A-Za-z0-9_.+-]+\.dll", dll):
            raise ValueError(f"Invalid runtime DLL path: {dll}")
    files = required + [Path(dll) for dll in sorted(dlls)]
    if any(not (build / name).is_file() for name in files):
        missing = [str(name) for name in files if not (build / name).is_file()]
        raise FileNotFoundError("Missing runtime file(s): " + ", ".join(missing))
    if not (ROOT / "LICENSE").is_file():
        raise FileNotFoundError("Missing LICENSE")
    return files


def build_archive(build: Path, version: str) -> Path:
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Invalid Windows release version")
    files = portable_files(build)
    root = f"Synchrorep-{version}"
    output = build / f"synchrorep-windows-{version}-x64-portable.zip"
    note = (
        "Synchrorep for Windows - portable edition\n"
        "\n"
        "Keep ALL files from this archive in the same folder.\n"
        "Do NOT run a separately downloaded synchrorep.exe: it depends\n"
        "on libcurl-4.dll, OpenSSL, libssh2 and other DLLs alongside it.\n"
        "\n"
        "Extract the complete archive first, then run synchrorep.exe.\n"
        "FTP and SFTP are included.\n"
        "\n"
        "For standard Windows installation, Explorer menu registration,\n"
        "and Start Menu shortcuts, use the Synchrorep MSI instead.\n"
        "Portable mode does not automatically register Explorer menus.\n"
    )
    checksums = []
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=7) as archive:
        for relative in files:
            source = build / relative
            contents = source.read_bytes()
            checksums.append(f"{hashlib.sha256(contents).hexdigest()}  {relative.as_posix()}")
            archive.writestr(f"{root}/{relative.as_posix()}", contents)
        license_contents = (ROOT / "LICENSE").read_bytes()
        checksums.append(f"{hashlib.sha256(license_contents).hexdigest()}  LICENSE")
        archive.writestr(f"{root}/LICENSE", license_contents)
        archive.writestr(f"{root}/README-FIRST.txt", note)
        archive.writestr(f"{root}/SHA256SUMS.txt", "\n".join(checksums) + "\n")
    print(f"Created {output} ({len(files)} executable/runtime files and license)")
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    build_archive(args.build, args.version)


if __name__ == "__main__":
    main()
