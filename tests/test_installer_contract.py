#!/usr/bin/env python3
"""Regression contracts for unattended installer behavior."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SETUP_SOURCE = (ROOT / "Setup" / "Setup.nsi").read_text(encoding="utf-8")
QT_CONFIG = (ROOT / "Setup" / "qt.conf").read_text(encoding="utf-8")
DEVICE_SELECTOR_SOURCE = (ROOT / "DeviceSelector" / "main.cpp").read_text(
    encoding="utf-8"
)
UPDATE_CHECKER_SOURCE = (ROOT / "UpdateChecker" / "main.cpp").read_text(
    encoding="utf-8"
)


class InstallerContractTests(unittest.TestCase):
    def test_silent_update_uses_headless_audio_restart(self) -> None:
        self.assertIn('${If} ${Silent}', SETUP_SOURCE)
        self.assertIn('DeviceSelector.exe" /r /s', SETUP_SOURCE)
        self.assertIn('arguments().contains("/r", Qt::CaseInsensitive)', DEVICE_SELECTOR_SOURCE)
        self.assertIn('ServiceHelper::restartService(L"AudioSrv")', DEVICE_SELECTOR_SOURCE)

    def test_silent_task_updates_do_not_show_error_dialogs(self) -> None:
        self.assertIn('UpdateChecker.exe" -i -s', SETUP_SOURCE)
        self.assertIn('UpdateChecker.exe" -u -s', SETUP_SOURCE)
        self.assertIn('QCommandLineOption silentOption("s"', UPDATE_CHECKER_SOURCE)
        self.assertGreaterEqual(UPDATE_CHECKER_SOURCE.count("if (!silentMode)"), 2)

    def test_installer_message_boxes_are_silent_guarded(self) -> None:
        # Fatal and warning messages stay available interactively, but every
        # installer MessageBox must be nested in an explicit silent guard.
        self.assertGreaterEqual(SETUP_SOURCE.count("MessageBox "), 7)
        self.assertGreaterEqual(SETUP_SOURCE.count('${IfNot} ${Silent}'), 7)

    def test_default_start_menu_folder_is_migrated_on_upgrade(self) -> None:
        self.assertIn('StrCpy $0 "$OldStartMenuFolder" 14', SETUP_SOURCE)
        self.assertIn('StrCpy $StartMenuFolder "Equalizer APO ${VERSION}"', SETUP_SOURCE)

    def test_qt_plugins_are_found_outside_the_install_working_directory(self) -> None:
        self.assertIn('File "qt.conf"', SETUP_SOURCE)
        self.assertIn('Delete "$INSTDIR\\qt.conf"', SETUP_SOURCE)
        self.assertEqual(QT_CONFIG.strip(), "[Paths]\nPlugins = qt")

    def test_installer_validates_payload_before_persistent_changes(self) -> None:
        self.assertIn("CRCCheck force", SETUP_SOURCE)
        self.assertIn('File /oname=NOTICE.md "..\\NOTICE.md"', SETUP_SOURCE)
        self.assertIn('!insertmacro RequireInstalledAsset "$INSTDIR\\NOTICE.md"', SETUP_SOURCE)
        self.assertLess(
            SETUP_SOURCE.index("Call VerifyRequiredAssets"),
            SETUP_SOURCE.index('WriteRegStr HKLM ${REGPATH} "InstallPath"'),
        )
        self.assertIn("Function SaveProtectedAudioSetting", SETUP_SOURCE)
        self.assertIn("Function RestoreProtectedAudioSetting", SETUP_SOURCE)

    def test_start_menu_cleanup_preserves_unrelated_shortcuts(self) -> None:
        self.assertIn("!macro DeleteProductShortcuts folder", SETUP_SOURCE)
        self.assertIn('CreateShortCut "$SMPROGRAMS\\$StartMenuFolder\\Check for updates.lnk"', SETUP_SOURCE)
        self.assertNotIn('RMDir /r "$SMPROGRAMS\\$OldStartMenuFolder"', SETUP_SOURCE)
        self.assertNotIn('RMDir /r "$SMPROGRAMS\\$StartMenuFolder"', SETUP_SOURCE)


if __name__ == "__main__":
    unittest.main()
