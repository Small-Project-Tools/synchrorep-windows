# Copyright (C) 2026 Sébastien Kus
# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parent.parent


class SignPathPolicyTest(unittest.TestCase):
    def test_public_policy_and_roles(self):
        policy = (ROOT / "docs/code-signing-policy.md").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        for text in ("Code signing policy", "Free code signing provided by SignPath.io, certificate by SignPath Foundation", "committer", "Reviewer", "approver", "Privacy policy", "unsigned"):
            self.assertIn(text.lower(), policy.lower())
        self.assertIn("docs/code-signing-policy.md", readme)

    def test_unsigned_state_is_explicit(self):
        policy = (ROOT / "docs/code-signing-policy.md").read_text(encoding="utf-8")
        self.assertIn("No certificate has been issued", policy)
        self.assertIn("before activation", policy)


if __name__ == "__main__":
    unittest.main()
