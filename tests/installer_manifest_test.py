#!/usr/bin/env python3
"""Validate the single, complete WiX3 MSI template (Linux CI)."""
import re
import xml.etree.ElementTree as ET
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=root/"windows/installer/Synchrorep.wxs"
text=source.read_text()
package=ET.fromstring(text)
ns={"w":"http://schemas.microsoft.com/wix/2006/wi"}
product=package.find("w:Product",ns)
cmake=(root/"CMakeLists.txt").read_text()
version=re.search(r"project\(synchrorep_windows VERSION (\d+\.\d+\.\d+)",cmake).group(1)
assert product is not None and product.attrib["Version"]==version
assert product.attrib["Name"]=="Synchrorep"
assert product.attrib["UpgradeCode"]=="E7933186-3749-4B61-AD35-68A48EB337CD"
assert product.find("w:Package",ns).attrib["InstallScope"]=="perMachine"
assert product.find("w:Property[@Id='ARPPRODUCTICON']",ns).attrib["Value"]=="SynchrorepIcon"
assert product.find("w:Icon",ns).attrib["SourceFile"]=="$(var.SourceIcon)"
files={f.attrib["Id"]:f.attrib["Source"] for f in product.findall(".//w:File",ns)}
assert files=={
    "AppFile":"$(var.SourceExe)",
    "ExtensionFile":"$(var.SourceExplorerDll)",
    "LicenseFile":"$(var.SourceLicense)",
    "ThirdPartyNoticesFile":"$(var.SourceNotices)",
}
shortcuts=product.findall(".//w:Shortcut",ns)
assert len(shortcuts)==1 and shortcuts[0].attrib["Name"]=="Synchrorep"
assert len(product.findall(".//w:RegistryValue",ns))==0
assert "<!-- RUNTIME_COMPONENTS -->" in text and "<!-- RUNTIME_REFS -->" in text
assert len(product.findall(".//w:Component",ns))==len(files)
assert len(product.findall(".//w:ComponentRef",ns))==len(files)
print("Single Windows MSI manifest validated")
