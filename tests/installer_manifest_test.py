#!/usr/bin/env python3
"""Validate the MSI manifest's structure and version during Linux CI."""
import re
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parents[1]
doc = ET.parse(root / "windows/installer/Synchrorep.wxs").getroot()
ns = {"w": "http://wixtoolset.org/schemas/v4/wxs"}
package = doc.find("w:Package", ns)
assert package is not None
cmake = (root / "CMakeLists.txt").read_text()
match = re.search(r"project\(synchrorep_windows VERSION (\d+\.\d+\.\d+)", cmake)
assert match and match.group(1) == package.attrib["Version"]
assert package.attrib["Scope"] == "perMachine"
assert re.fullmatch(r"[0-9A-Fa-f-]{36}", package.attrib["UpgradeCode"])
files = package.findall(".//w:File", ns)
assert {item.attrib["Id"] for item in files} == {"MainExecutable", "ExplorerExtension", "LicenseDocument"}
assert {item.attrib["Source"] for item in files} == {"$(var.SourceExe)", "$(var.SourceExplorerDll)", "$(var.SourceLicense)"}
assert len(package.findall(".//w:Component", ns)) == len(files)
assert len(package.findall(".//w:ComponentRef", ns)) == len(files)
icon=package.find("w:Icon",ns)
assert icon is not None and icon.attrib["SourceFile"]=="$(var.SourceIcon)"
arp=package.find("w:Property[@Id='ARPPRODUCTICON']",ns)
assert arp is not None and arp.attrib["Value"]=="SynchrorepIcon"
print("WiX MSI metadata validated")
