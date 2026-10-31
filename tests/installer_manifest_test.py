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
# Registration must be automatic for classic Windows 11 "Show more options".
registry=product.findall(".//w:RegistryValue",ns)
assert len(registry)==9
expected={
    "Software\\Classes\\Directory\\shell\\Synchrorep": '%1',
    "Software\\Classes\\Directory\\Background\\shell\\Synchrorep": '%V',
    "Software\\Classes\\Drive\\shell\\Synchrorep": '%1',
}
for root,argument in expected.items():
    items=[v for v in registry if v.attrib["Key"] in (root,root+"\\command")]
    assert len(items)==3
    assert all(v.attrib["Root"]=="HKLM" for v in items)
    title=next(v for v in items if v.attrib["Key"]==root and "Name" not in v.attrib)
    icon=next(v for v in items if v.attrib.get("Name")=="Icon")
    command=next(v for v in items if v.attrib["Key"]==root+"\\command")
    assert title.attrib["Value"]=="Synchronize with Synchrorep"
    assert icon.attrib["Value"]=='"[#AppFile]",0'
    assert command.attrib["Value"]=='"[#AppFile]" "'+argument+'"'
# Windows Installer owns the registry values through the AppComponent;
# they must be removed with that component on uninstall.
assert len(product.findall(".//w:RemoveRegistryKey",ns))==0
# Don't implicitly enable any synchronization, just the menu.
assert not any("\\Run" in v.attrib["Key"] for v in registry)

assert "<!-- RUNTIME_COMPONENTS -->" in text and "<!-- RUNTIME_REFS -->" in text
assert len(product.findall(".//w:Component",ns))==len(files)
assert len(product.findall(".//w:ComponentRef",ns))==len(files)
print("Single Windows MSI manifest validated")
