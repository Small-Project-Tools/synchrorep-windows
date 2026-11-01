#!/usr/bin/env python3
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
"""Static consistency checks for the Windows 11 sparse MSIX command package."""
import re
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parents[1]
xml = ET.parse(root / "windows/sparse/AppxManifest.xml").getroot()
ns = {
    "p": "http://schemas.microsoft.com/appx/manifest/foundation/windows10",
    "com": "http://schemas.microsoft.com/appx/manifest/com/windows10",
    "d4": "http://schemas.microsoft.com/appx/manifest/desktop/windows10/4",
    "d5": "http://schemas.microsoft.com/appx/manifest/desktop/windows10/5",
    "uap": "http://schemas.microsoft.com/appx/manifest/uap/windows10",
    "uap10": "http://schemas.microsoft.com/appx/manifest/uap/windows10/10",
}
project = (root / "CMakeLists.txt").read_text()
version = re.search(r"project\(synchrorep_windows VERSION ([0-9.]+)", project).group(1)
identity = xml.find("p:Identity", ns)
assert identity is not None
assert identity.attrib["Version"] == version + ".0"
assert identity.attrib["ProcessorArchitecture"] == "x64"
assert xml.find("./p:Properties/uap10:AllowExternalContent", ns).text == "true"
app = xml.find("./p:Applications/p:Application", ns)
assert app is not None and app.attrib["Executable"] == "synchrorep.exe"
assert app.find("uap:VisualElements", ns) is not None
classes = xml.findall(".//com:Class", ns)
verbs = xml.findall(".//d5:ItemType", ns)
assert len(classes) == 1
assert classes[0].attrib["Path"] == "SynchrorepExplorer.dll"
assert classes[0].attrib["ThreadingModel"] == "STA"
assert len(verbs) == 1 and verbs[0].attrib["Type"] == "Directory"
command = verbs[0].find("d5:Verb", ns)
assert command.attrib["Clsid"] == classes[0].attrib["Id"]
header = (root / "windows/explorer_clsid.hpp").read_text()
tokens = re.findall(r"0x[0-9a-fA-F]+", header)
assert len(tokens) == 11, tokens
values = [int(v, 16) for v in tokens]
guid = (
    f"{values[0]:08X}-{values[1]:04X}-{values[2]:04X}-"
    f"{values[3]:02X}{values[4]:02X}-"
    + "".join(f"{v:02X}" for v in values[5:])
)
assert command.attrib["Clsid"].upper() == guid
assert (root / "windows/sparse/package.ps1").exists()
assert not any((root / "windows/sparse").rglob("*.pfx"))
for file, dimension in (("Logo44.png", 44), ("Logo150.png", 150), ("StoreLogo.png", 50)):
    data = (root / "windows/sparse/Assets" / file).read_bytes()
    assert data.startswith(b"\x89PNG\r\n\x1a\n")
    assert data[12:16] == b"IHDR"
    width, height = struct.unpack(">II", data[16:24])
    assert (width, height) == (dimension, dimension)
print("Sparse MSIX manifest, assets and COM CLSID validated")
