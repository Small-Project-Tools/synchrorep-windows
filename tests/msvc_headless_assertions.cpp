// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
// Debug assertions must report to stderr instead of blocking a headless CI
// runner with the Visual C++ assertion dialog.
#if defined(_MSC_VER) && defined(_DEBUG)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <crtdbg.h>
namespace {
struct HeadlessTestAssertions {
 HeadlessTestAssertions() {
  _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 }
};
HeadlessTestAssertions setup_headless_assertions;
}
#endif
