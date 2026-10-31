#!/usr/bin/env python3
"""Fetch MSYS2 MinGW64 curl/OpenSSL + runtime deps for Windows CI.

Only reads the MSYS2 HTTPS repository metadata and checksum-verified archives.
No untrusted archive metadata may write outside the selected output directory.
"""
import argparse
import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import urllib.request

REPOSITORY = "https://repo.msys2.org/mingw/mingw64/"
ROOT_PACKAGE = "mingw-w64-x86_64-curl"
MIN_VERSION = (8, 20, 0)
HEADERS = {"User-Agent": "Synchrorep Windows cross-build/1"}

def fetch(url, destination):
    request = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(request, timeout=100) as resp, destination.open("wb") as out:
        while True:
            block = resp.read(256 * 1024)
            if not block:
                break
            out.write(block)

def decode_archive(path):
    raw = subprocess.check_output(["zstd", "-d", "-c", str(path)])
    return tarfile.open(fileobj=io.BytesIO(raw), mode="r:")

def sections(raw):
    result = {}
    chunks = raw.decode("utf-8").strip().split("\n\n")
    for chunk in chunks:
        lines=chunk.splitlines()
        if lines and lines[0].startswith("%") and lines[0].endswith("%"):
            result[lines[0][1:-1]] = lines[1:]
    return result

def metadata(db_file):
    packages={}
    with decode_archive(db_file) as archive:
        for item in archive.getmembers():
            if not item.name.endswith("/desc"):
                continue
            data=sections(archive.extractfile(item).read())
            name=data["NAME"][0]
            packages[name]=data
    return packages

def collect_packages(packages):
    selected={}
    def add(name):
        name=re.split(r"[<>=]",name,1)[0]
        if name in selected:
            return
        entry=packages.get(name)
        if entry is None:
            raise RuntimeError("Missing MSYS2 dependency: "+name)
        selected[name]=entry
        for dependency in entry.get("DEPENDS", []):
            add(dependency)
    add(ROOT_PACKAGE)
    add("mingw-w64-x86_64-sqlite3")
    return selected

def unpack_package(package_path, destination, package_name, licenses):
    extracted=0
    with decode_archive(package_path) as archive:
        for member in archive:
            if not member.isfile():
                continue
            name=PurePosixPath(member.name)
            parts=name.parts
            if len(parts)<3 or parts[0] not in ("mingw64","./mingw64"):
                continue
            # Includes curl headers/import library and dependent Windows DLLs.
            is_dll=(parts[1]=="bin" and name.suffix.lower()==".dll")
            is_sqlite_header=(package_name=="mingw-w64-x86_64-sqlite3" and parts[1]=="include" and name.name in ("sqlite3.h","sqlite3ext.h"))
            is_sqlite_import=(package_name=="mingw-w64-x86_64-sqlite3" and parts[1]=="lib" and name.name=="libsqlite3.dll.a")
            is_curl_header=(package_name==ROOT_PACKAGE and parts[1]=="include"
                            and len(parts)>=4 and parts[2]=="curl")
            is_curl_import=(package_name==ROOT_PACKAGE and parts[1]=="lib"
                            and name.name=="libcurl.dll.a")
            is_license=(parts[1]=="share" and len(parts)>=5
                        and parts[2]=="licenses")
            if not any((is_dll,is_curl_header,is_curl_import,is_sqlite_header,is_sqlite_import,is_license)):
                continue
            # Strip /mingw64 and ensure only regular files are extracted.
            if is_license:
                output=licenses/package_name/name.name
            else:
                output=destination.joinpath(*parts[1:])
            output.parent.mkdir(parents=True,exist_ok=True)
            with archive.extractfile(member) as source, output.open("wb") as target:
                while True:
                    data=source.read(256*1024)
                    if not data:break
                    target.write(data)
            extracted+=1
    return extracted

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--output",type=Path,default=Path("build/msys2-mingw64"))
    parser.add_argument("--cache",type=Path,default=Path("build/msys2-cache"))
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    args.cache.mkdir(parents=True,exist_ok=True)
    database=args.cache/"mingw64.db"
    fetch(REPOSITORY+"mingw64.db",database)
    packages=metadata(database)
    selected=collect_packages(packages)
    version=packages[ROOT_PACKAGE]["VERSION"][0]
    numbers=tuple(int(s) for s in version.split("-")[0].split(".")[:3])
    if numbers<MIN_VERSION:
        raise RuntimeError("Outdated MSYS2 curl version: "+version)
    manifest=[]
    for name in sorted(selected):
        data=selected[name]
        filename=data["FILENAME"][0]
        checksum=data["SHA256SUM"][0].lower()
        if not re.fullmatch(r"[a-zA-Z0-9_.+-]+\.pkg\.tar\.zst",filename):
            raise RuntimeError("Invalid package filename")
        target=args.cache/filename
        if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest()!=checksum:
            fetch(REPOSITORY+filename,target)
        if hashlib.sha256(target.read_bytes()).hexdigest()!=checksum:
            raise RuntimeError("Checksum mismatch for "+filename)
        extracted=unpack_package(target,args.output,name,args.output/"licenses")
        print(f"{name} ({data['VERSION'][0]}): {extracted} files",flush=True)
        manifest.append(f"{name} {data['VERSION'][0]} {checksum}")
    (args.output/"dependencies.lock").write_text("\n".join(manifest)+"\n")
    curl_import=args.output/"lib/libcurl.dll.a"
    header=args.output/"include/curl/curl.h"
    dll=args.output/"bin/libcurl-4.dll"
    if not all(item.is_file() for item in (curl_import,header,dll)):
        raise RuntimeError("libcurl Windows headers, DLL or import library missing")
    dlls=list((args.output/"bin").glob("*.dll"))
    if len(dlls)<3:
        raise RuntimeError("Missing MSYS2 dependent DLLs")
    print(f"Selected {len(selected)} checksum-verified package archives; {len(dlls)} Windows DLLs")
    print(f"libcurl Windows version: {version}")

if __name__=="__main__":
    main()
