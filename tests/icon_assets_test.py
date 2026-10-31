#!/usr/bin/env python3
"""Verify the image supplied for Synchrorep is embedded consistently."""
import hashlib
import struct
import zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
src=ROOT/"assets/synchrorep-source.png"
assert hashlib.sha256(src.read_bytes()).hexdigest()=="51b4bee3f4d6e35787acbd0f76281c35bfdaeba2f3ec8e5188518b3c3d9c3bcf"
script=(ROOT/"scripts/build_icon_assets.py").read_text()
assert "synchrorep-source.png" in script
assert (ROOT/"windows/synchrorep.rc").read_text().count('101 ICON "assets/synchrorep.ico"')==1
asset=ROOT/"assets/synchrorep.ico"
blob=asset.read_bytes()
reserved,kind,count=struct.unpack_from("<HHH",blob)
assert (reserved,kind,count)==(0,1,7)
expected=[16,24,32,48,64,128,256]
for i,size in enumerate(expected):
    width,height,colors,reserved,planes,bits,length,offset=struct.unpack_from("<BBBBHHII",blob,6+i*16)
    assert (width or 256,height or 256)==(size,size)
    assert planes==1 and bits==32
    img=blob[offset:offset+length]
    if size==256:
        assert img[:8]==b"\x89PNG\r\n\x1a\n"
        assert struct.unpack_from(">II",img,16)==(size,size)
        assert zlib.crc32(img[12:16]+img[16:29])&0xffffffff==struct.unpack_from(">I",img,29)[0]
    else:
        assert struct.unpack_from("<IiiHH",img)==(40,size,size*2,1,32)
        assert len(img)==40+size*size*4+((size+31)//32)*4*size
for name,size in (("Logo44.png",44),("Logo150.png",150),("StoreLogo.png",50)):
    image=(ROOT/"windows/sparse/Assets"/name).read_bytes()
    assert image[:8]==b"\x89PNG\r\n\x1a\n"
    assert struct.unpack_from(">II",image,16)==(size,size)
    assert image!=b""
main=(ROOT/"windows/main.cpp").read_text()
assert "LoadIconW(instance,MAKEINTRESOURCEW(101))" in main
assert "LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101))" in main
explorer=(ROOT/"windows/explorer_command.cpp").read_text()
assert 'L"synchrorep.exe"' in explorer and 'L",0"' in explorer
print("Synchrorep original-derived image, Windows ICO and MSIX icons validated")

# The notification icon needs both legacy and v4 shell events to expose
# the same context menu on modern Windows 11 builds.
main=(ROOT/"windows/main.cpp").read_text()
assert "NIM_SETVERSION" in main and "NOTIFYICON_VERSION_4" in main
assert "LOWORD(l)" in main and "event==WM_CONTEXTMENU" in main
assert "event==WM_RBUTTONUP" in main and "TrackPopupMenu" in main
