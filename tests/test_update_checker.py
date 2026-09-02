#!/usr/bin/env python3
"""Regression contracts for the fork-specific update checker."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_SOURCE = (ROOT / "UpdateChecker" / "main.cpp").read_text(encoding="utf-8")
DIALOG_SOURCE = (ROOT / "UpdateChecker" / "UpdateChecker.cpp").read_text(
    encoding="utf-8"
)


class UpdateCheckerTests(unittest.TestCase):
    def test_checks_this_forks_https_github_release(self) -> None:
        self.assertIn(
            "https://api.github.com/repos/xup61069/"
            "loudness-correction-apo/releases/latest",
            MAIN_SOURCE,
        )
        self.assertNotIn("equalizerapo.sourceforge.io/checkVersion", MAIN_SOURCE)
        self.assertIn("QVersionNumber::compare", MAIN_SOURCE)

    def test_network_request_has_timeout_and_explicit_api_headers(self) -> None:
        self.assertIn("request.setTransferTimeout", MAIN_SOURCE)
        self.assertIn('request.setRawHeader("Accept"', MAIN_SOURCE)
        self.assertIn('request.setRawHeader("X-GitHub-Api-Version"', MAIN_SOURCE)
        self.assertIn("NoLessSafeRedirectPolicy", MAIN_SOURCE)

    def test_remote_release_text_is_html_escaped(self) -> None:
        self.assertGreaterEqual(DIALOG_SOURCE.count("toHtmlEscaped()"), 3)
        self.assertIn('downloadUrl.scheme() != "https"', DIALOG_SOURCE)

    def test_silent_failures_return_nonzero_without_dialogs(self) -> None:
        self.assertIn("showFailureMessage(QString message, QString title, bool silentMode)", MAIN_SOURCE)
        self.assertIn("if (silentMode)\n\t\treturn;", MAIN_SOURCE)
        self.assertGreaterEqual(MAIN_SOURCE.count("result = 2;"), 3)
        self.assertIn("if (!autoMode && !silentMode)", MAIN_SOURCE)


if __name__ == "__main__":
    unittest.main()
