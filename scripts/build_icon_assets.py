#!/usr/bin/env python3
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
"""Rebuild Windows icon assets using only Python's standard library."""
from __future__ import annotations
import hashlib, struct, zlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"assets/synchrorep-source.png"
EXPECTED_SHA="51b4bee3f4d6e35787acbd0f76281c35bfdaeba2f3ec8e5188518b3c3d9c3bcf"

def chunks(blob):
    assert blob.startswith(b"\x89PNG\r\n\x1a\n")
    cursor=8
    while cursor<len(blob):
        length=struct.unpack_from(">I",blob,cursor)[0]
        kind=blob[cursor+4:cursor+8]
        payload=blob[cursor+8:cursor+8+length]
        expected=struct.unpack_from(">I",blob,cursor+8+length)[0]
        assert zlib.crc32(kind+payload)&0xffffffff==expected,kind
        yield kind,payload
        cursor+=12+length

def unpack_png(blob):
    width=height=0;palette=b"";compressed=b"";bits=0;mode=0
    for kind,data in chunks(blob):
        if kind==b"IHDR":
            width,height,bits,mode,_,_,_=struct.unpack(">IIBBBBB",data)
        elif kind==b"PLTE":palette=data
        elif kind==b"IDAT":compressed+=data
    assert (width,height,bits,mode)==(128,128,8,3)
    data=zlib.decompress(compressed)
    result=[];prev=bytearray(width);pos=0
    for _ in range(height):
        filt=data[pos];pos+=1;row=bytearray(data[pos:pos+width]);pos+=width
        for i in range(width):
            a=row[i-1] if i else 0
            b=prev[i];c=prev[i-1] if i else 0
            if filt==1:row[i]=(row[i]+a)&255
            elif filt==2:row[i]=(row[i]+b)&255
            elif filt==3:row[i]=(row[i]+((a+b)//2))&255
            elif filt==4:
                p=a+b-c
                delta=(abs(p-a),abs(p-b),abs(p-c))
                predictor=(a,b,c)[delta.index(min(delta))]
                row[i]=(row[i]+predictor)&255
            else:assert filt==0,filt
        result.append([tuple(palette[3*i:3*i+3]) for i in row])
        prev=row
    return result

def resized(source,side):
    original=len(source)
    result=[]
    for y in range(side):
        sy=(y+.5)*original/side-.5
        y0=max(0,min(int(sy),original-1));y1=min(y0+1,original-1)
        dy=max(0,min(1,sy-y0))
        row=[]
        for x in range(side):
            sx=(x+.5)*original/side-.5
            x0=max(0,min(int(sx),original-1));x1=min(x0+1,original-1)
            dx=max(0,min(1,sx-x0))
            rgb=[]
            for c in range(3):
                top=source[y0][x0][c]*(1-dx)+source[y0][x1][c]*dx
                bottom=source[y1][x0][c]*(1-dx)+source[y1][x1][c]*dx
                rgb.append(round(top*(1-dy)+bottom*dy))
            row.append(tuple(rgb))
        result.append(row)
    return result

def chunk(kind,content):
    return struct.pack(">I",len(content))+kind+content+struct.pack(">I",zlib.crc32(kind+content)&0xffffffff)

def rgba_png(pixels):
    side=len(pixels)
    raw=b"".join(b"\x00"+b"".join(bytes(rgb)+(b"\xff") for rgb in row) for row in pixels)
    header=struct.pack(">IIBBBBB",side,side,8,6,0,0,0)
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",header)+chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b"")

def bitmap_icon(pixels):
    # Classic 32-bit DIB + 1-bit AND mask. Windows Shell and Wine support
    # these at small sizes more consistently than PNG-only ICO entries.
    side=len(pixels)
    info=struct.pack("<IiiHHIIiiII",40,side,side*2,1,32,0,side*side*4,0,0,0,0)
    bgra=b"".join(bytes((blue,green,red,255))
                  for row in reversed(pixels) for red,green,blue in row)
    mask=b"\x00"*(((side+31)//32)*4*side)
    return info+bgra+mask

def main():
    content=SOURCE.read_bytes()
    assert hashlib.sha256(content).hexdigest()==EXPECTED_SHA,"Unexpected or corrupted source icon"
    pixels=unpack_png(content)
    assetdir=ROOT/"windows/sparse/Assets"
    assetdir.mkdir(parents=True,exist_ok=True)
    for name,side in (("Logo44.png",44),("Logo150.png",150),("StoreLogo.png",50)):
        (assetdir/name).write_bytes(rgba_png(resized(pixels,side)))
    # Use DIB frames for small sizes (robust in Explorer and Wine), and a
    # compressed PNG for the modern 256x256 frame.
    sizes=(16,24,32,48,64,128,256)
    images=[bitmap_icon(resized(pixels,n)) if n<256
            else rgba_png(resized(pixels,n)) for n in sizes]
    offset=6+16*len(images);table=b""
    for side,image in zip(sizes,images):
        table+=struct.pack("<BBBBHHII",side if side<256 else 0,
            side if side<256 else 0,0,0,1,32,len(image),offset)
        offset+=len(image)
    ico=struct.pack("<HHH",0,1,len(images))+table+b"".join(images)
    (ROOT/"assets/synchrorep.ico").write_bytes(ico)
    print("Generated matching PNG logos and ICO frames:",sizes,
          "ICO bytes",len(ico))

if __name__=="__main__":main()
