#!/usr/bin/env python3
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
import os
import socket
import threading
import paramiko

ROOT = os.path.realpath(os.environ.get("SYNCHROREP_TEST_SFTP_ROOT", "/tmp/synchrorep-test-sftp"))
os.makedirs(ROOT, exist_ok=True)
KEY = paramiko.RSAKey.generate(2048)
with open(os.environ.get("SYNCHROREP_TEST_KNOWN_HOSTS", "/tmp/synchrorep-test-known-hosts"), "w", encoding="utf8") as f:
    f.write(f"[127.0.0.1]:2222 {KEY.get_name()} {KEY.get_base64()}\n")

def resolve(path):
    path = path.lstrip("/")
    target = os.path.realpath(os.path.join(ROOT, path))
    if os.path.commonpath((ROOT,target)) != ROOT:
        raise OSError("Path outside test root")
    return target

class Auth(paramiko.ServerInterface):
    def check_auth_password(self, username, password):
        if username == "tester" and password == "secret":
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED
    def get_allowed_auths(self, username):
        return "password"
    def check_channel_request(self, kind, channel_id):
        if kind == "session":
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

class Files(paramiko.SFTPServerInterface):
    def list_folder(self, path):
        try:
            result=[]
            for entry in os.scandir(resolve(path)):
                attr=paramiko.SFTPAttributes.from_stat(entry.stat(follow_symlinks=False))
                attr.filename=entry.name
                result.append(attr)
            return result
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)
    def stat(self, path):
        try:return paramiko.SFTPAttributes.from_stat(os.stat(resolve(path)))
        except OSError as e:return paramiko.SFTPServer.convert_errno(e.errno)
    lstat=stat
    def open(self, path, flags, attr):
        try:
            name=resolve(path)
            mode="rb"
            if flags & os.O_WRONLY:mode="wb"
            if flags & os.O_RDWR:mode="r+b"
            if flags & os.O_CREAT:mode="wb+" if flags & os.O_RDWR else "wb"
            if flags & os.O_EXCL:mode="xb+" if flags & os.O_RDWR else "xb"
            f=open(name,mode)
            handle=paramiko.SFTPHandle(flags)
            handle.readfile=f
            handle.writefile=f
            return handle
        except OSError as e:return paramiko.SFTPServer.convert_errno(e.errno)
    def mkdir(self,path,attr):
        try:os.mkdir(resolve(path));return paramiko.SFTP_OK
        except OSError as e:return paramiko.SFTPServer.convert_errno(e.errno)
    def rmdir(self,path):
        try:os.rmdir(resolve(path));return paramiko.SFTP_OK
        except OSError as e:return paramiko.SFTPServer.convert_errno(e.errno)
    def remove(self,path):
        try:os.remove(resolve(path));return paramiko.SFTP_OK
        except OSError as e:return paramiko.SFTPServer.convert_errno(e.errno)
    def rename(self,oldpath,newpath):
        try:
            os.replace(resolve(oldpath),resolve(newpath))
            return paramiko.SFTP_OK
        except OSError as e:return paramiko.SFTPServer.convert_errno(e.errno)

def serve(sock):
    try:
        transport=paramiko.Transport(sock)
        transport.add_server_key(KEY)
        transport.set_subsystem_handler("sftp",paramiko.SFTPServer,Files)
        transport.start_server(server=Auth())
        while transport.is_active():
            threading.Event().wait(0.1)
        transport.close()
    except Exception as exc:
        print("SFTP connection error:",exc,flush=True)

server=socket.socket()
server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
server.bind(("127.0.0.1",2222))
server.listen(20)
print("SFTP sandbox ready",flush=True)
while True:
    sock,addr=server.accept()
    threading.Thread(target=serve,args=(sock,),daemon=True).start()
