#!/usr/bin/env python3
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
import os
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer
root = os.environ.get("SYNCHROREP_TEST_FTP_ROOT", "/tmp/synchrorep-test-ftp")
os.makedirs(root, exist_ok=True)
auth = DummyAuthorizer()
auth.add_user("tester","secret",root,perm="elradfmwMT")
handler = FTPHandler
handler.authorizer = auth
handler.passive_ports = range(31500,31510)
FTPServer(("127.0.0.1", 2121),handler).serve_forever()
