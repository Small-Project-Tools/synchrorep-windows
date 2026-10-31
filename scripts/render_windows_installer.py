#!/usr/bin/env python3
"""Generate the single WiX3 MSI manifest from the verified Windows runtime."""
import argparse
import re
import uuid
from pathlib import Path

NS=uuid.NAMESPACE_URL

def guid(key):
    return str(uuid.uuid5(NS, "Synchrorep-x64/"+key)).upper()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--build",type=Path,required=True)
    parser.add_argument("--version",required=True)
    parser.add_argument("--template",type=Path,default=Path("windows/installer/Synchrorep.wxs"))
    args=parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+",args.version):
        raise ValueError("Invalid MSI version")
    template=args.template.read_text()
    if '<!-- RUNTIME_COMPONENTS -->' not in template or '<!-- RUNTIME_REFS -->' not in template:
        raise ValueError("Missing runtime component insertion points")
    template=template.replace('Version="0.13.2"',f'Version="{args.version}"')
    template=template.replace('Product Id="*"',
        f'Product Id="{guid("Product/"+args.version)}"')
    template=template.replace('''<Component Id="ThirdPartyNotices" Guid="*">''',
        f'''<Component Id="ThirdPartyNotices" Guid="{guid("Component/notices")}">''')
    dlls=[line.strip() for line in (args.build/"windows-dlls.txt").read_text().splitlines() if line.strip()]
    if not dlls or "libcurl-4.dll" not in dlls or "libssh2-1.dll" not in dlls:
        raise ValueError("Full FTP/SFTP runtime is missing")
    xml=[];refs=[]
    for i,dll in enumerate(sorted(set(dlls))):
        if not re.fullmatch(r"[a-zA-Z0-9_.-]+\.dll",dll):
            raise ValueError("Unsafe DLL file name")
        if not (args.build/dll).is_file():
            raise ValueError("Missing DLL "+dll)
        key="Runtime"+str(i)
        xml.append(f'''          <Component Id="{key}" Guid="{guid("Component/"+dll)}" Win64="yes">
            <File Id="{key}File" Source="$(var.RuntimeDir)/{dll}" Name="{dll}" KeyPath="yes"/>
          </Component>''')
        refs.append(f'      <ComponentRef Id="{key}"/>')
    template=template.replace("<!-- RUNTIME_COMPONENTS -->","\n".join(xml))
    template=template.replace("<!-- RUNTIME_REFS -->","\n".join(refs))
    output=args.build/"Synchrorep.wxs"
    output.write_text(template)
    print(f"Created unified MSI manifest with {len(dlls)} DLLs: {output}")

if __name__=="__main__":
    main()
