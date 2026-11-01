#!/usr/bin/env python3
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
"""The MSI must include transitive imports of both the app and Explorer COM DLL."""
import importlib.util
import tempfile
from pathlib import Path
from unittest.mock import patch
import sys

spec=importlib.util.spec_from_file_location("runtime_stage",Path(__file__).resolve().parents[1]/"scripts/stage_windows_runtime.py")
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    prefix=root/"prefix"
    (prefix/"bin").mkdir(parents=True)
    (prefix/"licenses").mkdir(parents=True)
    (prefix/"dependencies.lock").write_text("verified packages\n")
    build=root/"build"
    build.mkdir()
    for name in ("synchrorep.exe","SynchrorepExplorer.dll"):
        (build/name).write_bytes(b"PE")
    for name in ("libcurl-4.dll","common.dll","explorer-only.dll","deep.dll"):
        (prefix/"bin"/name).write_bytes(b"PE")
    graph={
        "synchrorep.exe":{"kernel32.dll","common.dll"},
        "synchrorepExplorer.dll".lower():{"ole32.dll","explorer-only.dll"},
        "common.dll":{"deep.dll"},
        "explorer-only.dll":set(),
        "deep.dll":set(),
    }
    old=sys.argv
    sys.argv=["stage_windows_runtime.py","--prefix",str(prefix),"--build",str(build)]
    try:
        with patch.object(module,"imports",side_effect=lambda path:graph[path.name.lower()]):
            module.main()
    finally:
        sys.argv=old
    assert (build/"windows-dlls.txt").read_text().splitlines()==[
        "common.dll","deep.dll","explorer-only.dll"]
    # The manifest is the exact list embedded in the MSI by render_windows_installer.
    for dll in (build/"windows-dlls.txt").read_text().splitlines():
        assert (build/dll).is_file()
print("Windows executable + Explorer COM import closure verified")
