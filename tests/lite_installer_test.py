#!/usr/bin/env python3
# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
"""Compatibility alias: the Lite variant no longer exists.

All Windows distributions must use the single Synchrorep MSI manifest.
"""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name("installer_manifest_test.py")),run_name="__main__")
