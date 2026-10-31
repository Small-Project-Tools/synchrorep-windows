#!/usr/bin/env python3
"""Verify actual bundled FTP/SFTP runtime and generated WiX MSI components."""
import argparse
from pathlib import Path
import re
import uuid
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser()
p.add_argument("--build",type=Path,required=True)
p.add_argument("--prefix",type=Path,default=Path("build-msys2/win64"))
args=p.parse_args()
build=args.build
cmake=(Path("CMakeLists.txt")).read_text()
version=re.search(r"project\(synchrorep_windows VERSION (\d+\.\d+\.\d+)",cmake).group(1)
ns={"w":"http://schemas.microsoft.com/wix/2006/wi"}
tree=ET.parse(build/"Synchrorep.wxs")
product=tree.getroot().find("w:Product",ns)
assert product is not None and product.attrib["Name"]=="Synchrorep"
assert product.attrib["Version"]==version
assert product.attrib["Id"]==str(uuid.uuid5(uuid.NAMESPACE_URL,"Synchrorep-x64/Product/"+version)).upper()
listed=(build/"windows-dlls.txt").read_text().splitlines()
assert len(listed)==len(set(listed)) and len(listed)>=5
assert {"libcurl-4.dll","libssh2-1.dll"}.issubset(listed)
all_dlls=[p.name for p in build.glob("*.dll")]
assert set(listed).issubset(all_dlls)
files=product.findall(".//w:File",ns)
runtime=[f for f in files if f.attrib["Name"].lower().endswith(".dll") and
         f.attrib["Name"]!="SynchrorepExplorer.dll"]
assert {f.attrib["Name"] for f in runtime}==set(listed)
assert len(runtime)==len(listed)
assert len(product.findall(".//w:Component",ns))==len(files)
assert len(product.findall(".//w:ComponentRef",ns))==len(files)
assert len({c.attrib["Guid"] for c in product.findall(".//w:Component",ns)})==len(files)
assert (build/"THIRD_PARTY_NOTICES.txt").stat().st_size>500
lock=args.prefix/"dependencies.lock"
assert lock.exists() and len(lock.read_text().splitlines())>=5
assert "mingw-w64-x86_64-curl " in lock.read_text().lower()
assert "libssh2" in lock.read_text().lower() and "openssl" in lock.read_text().lower()
assert "<!-- RUNTIME" not in (build/"Synchrorep.wxs").read_text()
print(f"Unified MSI validated: {len(listed)} curl and SSH runtime DLLs, license notices included")
