#!/usr/bin/env python3
"""Stage verified Windows curl runtime and its third-party license notices.

Produces DLLs beside the Windows executable; validates import closure via
MinGW objdump. The MSI then installs exactly those dependencies.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

SYSTEM_DLLS={
    "advapi32.dll","bcrypt.dll","comctl32.dll","comdlg32.dll","crypt32.dll",
    "gdi32.dll","iphlpapi.dll","kernel32.dll","msvcrt.dll","ntdll.dll",
    "ole32.dll","oleaut32.dll","rpcrt4.dll","secur32.dll","shell32.dll",
    "shlwapi.dll","user32.dll","userenv.dll","version.dll","wldap32.dll",
    "ws2_32.dll","winmm.dll","setupapi.dll","dnsapi.dll","normaliz.dll",
    "imm32.dll","uxtheme.dll","dwmapi.dll","kernelbase.dll"
}

def imports(file):
    result=subprocess.run(["x86_64-w64-mingw32-objdump","-p",str(file)],
        check=True,text=True,stdout=subprocess.PIPE).stdout
    return {name.lower() for name in re.findall(r"DLL Name:\s*(\S+)",result)}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--prefix",type=Path,required=True)
    parser.add_argument("--build",type=Path,required=True)
    options=parser.parse_args()
    bindir=options.prefix/"bin"
    if not (bindir/"libcurl-4.dll").is_file():
        raise RuntimeError("Windows curl runtime missing")
    if not (options.build/"synchrorep.exe").is_file():
        raise RuntimeError("Windows Synchrorep binary missing")
    # Include only dependencies reachable from the shipped executable.
    staged=set()
    requested=imports(options.build/"synchrorep.exe")-SYSTEM_DLLS
    while requested:
        dependency=requested.pop().lower()
        if dependency in SYSTEM_DLLS or dependency in staged:
            continue
        dlls={p.name.lower():p for p in bindir.glob("*.dll")}
        source=dlls.get(dependency)
        if source is None:
            raise RuntimeError("Unresolved Windows DLL dependency: "+dependency)
        staged.add(dependency)
        requested|=imports(source)-SYSTEM_DLLS-staged
    # Stage all verified runtime DLLs beside test executables so Windows
    # integration tests can load MinGW's runtime, but put ONLY the reachable
    # application dependency closure into the MSI manifest.
    for file in bindir.glob("*.dll"):
        shutil.copy2(file, options.build/file.name)
    for entry in imports(options.build/"synchrorep.exe"):
        if entry not in SYSTEM_DLLS and entry not in staged:
            raise RuntimeError("Executable imports missing DLL: "+entry)
    # Runtime package notices, kept separate from the project's own license.
    notices=[]
    lock=(options.prefix/"dependencies.lock").read_text()
    notices.append("Synchrorep for Windows - bundled third-party notices\n")
    notices.append("MSYS2 package versions and verified SHA256:\n"+lock+"\n")
    for file in sorted((options.prefix/"licenses").rglob("*")):
        if file.is_file():
            notices.append("\n========== "+str(file.relative_to(options.prefix/"licenses"))+" ==========\n")
            notices.append(file.read_bytes().decode("utf-8",errors="replace"))
    (options.build/"THIRD_PARTY_NOTICES.txt").write_text("\n".join(notices),encoding="utf-8")
    (options.build/"windows-dlls.txt").write_text("\n".join(sorted(staged))+"\n")
    print(f"Staged {len(staged)} Windows DLLs with third-party licenses")
    for dll in sorted(staged):print("  "+dll)

if __name__=="__main__":
    main()
