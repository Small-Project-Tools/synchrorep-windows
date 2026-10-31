#!/usr/bin/env python3
"""Verify complete portable distribution, checksums and contents."""
import argparse
import hashlib
import re
from pathlib import Path
from zipfile import ZipFile

parser=argparse.ArgumentParser()
parser.add_argument("--build",type=Path,required=True)
parser.add_argument("--version",required=True)
args=parser.parse_args()

archive=args.build/f"synchrorep-windows-{args.version}-x64-portable.zip"
root=f"Synchrorep-{args.version}/"
required={"synchrorep.exe","SynchrorepExplorer.dll","LICENSE",
          "THIRD_PARTY_NOTICES.txt","README-FIRST.txt","SHA256SUMS.txt"}
dlls=set((args.build/"windows-dlls.txt").read_text(encoding="utf-8").splitlines())
assert {"libcurl-4.dll","libssh2-1.dll"}.issubset(dlls)
assert len(dlls)>=5
with ZipFile(archive) as zipfile:
    names=zipfile.namelist()
    expected={root+name for name in required|dlls}
    assert len(names)==len(set(names))
    assert set(names)==expected, (set(names)-expected, expected-set(names))
    assert all(name.startswith(root) for name in names)
    readme=zipfile.read(root+"README-FIRST.txt").decode("utf-8")
    assert "libcurl-4.dll" in readme and "same folder" in readme
    data=zipfile.read(root+"SHA256SUMS.txt").decode("ascii")
    checked=set()
    for entry in data.splitlines():
        match=re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_.+-]+)",entry)
        assert match,entry
        checksum,filename=match.groups()
        assert checksum==hashlib.sha256(zipfile.read(root+filename)).hexdigest()
        checked.add(filename)
    assert checked==(required|dlls)-{"README-FIRST.txt","SHA256SUMS.txt"}
    for dll in dlls:
        assert zipfile.read(root+dll)==(args.build/dll).read_bytes()
    assert zipfile.read(root+"synchrorep.exe")==(args.build/"synchrorep.exe").read_bytes()
print(f"Portable Windows bundle validated: {len(dlls)} runtime DLLs, SHA256 manifest and complete binary")
