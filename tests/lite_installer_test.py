#!/usr/bin/env python3
"""Validate the offline, cross-built Windows MSI (local/WSL edition)."""
import re
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parents[1]
xml = ET.parse(root / "windows/installer/Synchrorep-Lite.wxs").getroot()
ns = {"w": "http://schemas.microsoft.com/wix/2006/wi"}
product = xml.find("w:Product", ns)
assert product is not None
cmake = (root / "CMakeLists.txt").read_text()
version = re.search(r"project\(synchrorep_windows VERSION (\d+\.\d+\.\d+)", cmake)
assert version and product.attrib["Version"] == version.group(1)
assert product.attrib["UpgradeCode"] == "E7933186-3749-4B61-AD35-68A48EB337CD"
assert re.fullmatch(r"[A-F0-9]{8}(?:-[A-F0-9]{4}){3}-[A-F0-9]{12}", product.attrib["Id"])
assert product.attrib["Id"] != "*"
files = product.findall(".//w:File", ns)
assert {f.attrib["Id"] for f in files} == {"AppFile", "ExtensionFile", "LicenseFile"}
assert {f.attrib["Source"] for f in files} == {
    "$(var.SourceExe)", "$(var.SourceExplorerDll)", "$(var.SourceLicense)"
}
components = product.findall(".//w:Component", ns)
assert len(components) == len(files)
assert len({component.attrib["Guid"] for component in components}) == len(files)
assert all(component.attrib["Guid"] != "*" for component in components)
assert len(product.findall(".//w:ComponentRef", ns)) == len(files)
assert len(product.findall(".//w:Shortcut", ns)) == 1
assert product.find("w:Package", ns).attrib["InstallScope"] == "perMachine"
assert (root / "windows/installer/Synchrorep.wxs").exists()
# Installing the application must NEVER implicitly enable a destructive mirror.
assert len(product.findall(".//w:RegistryValue", ns)) == 0
print("Cross-built Lite MSI configuration verified")
