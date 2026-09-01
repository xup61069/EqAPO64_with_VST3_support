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

    def test_failed_registration_rolls_back_the_application_tree(self) -> None:
        prepare_call = "Call PrepareInstallTransaction"
        install_new_dll = 'File "${BINPATH}\\EqualizerAPO.dll"'
        failed_registration = "${If} $1 != 0"
        restore_call = "Call RollbackInstallTransaction"
        commit_call = "Call CommitInstallTransaction"

        self.assertIn("GetTempFileName $InstallRollbackDirectory", SETUP_SOURCE)
        self.assertNotIn("$PLUGINSDIR\\install-rollback", SETUP_SOURCE)
        self.assertIn(
            '"$SYSDIR\\robocopy.exe" "$INSTDIR" '
            '"$InstallRollbackFiles" /E /COPY:DATS',
            SETUP_SOURCE,
        )
        self.assertIn(
            '/XD "$INSTDIR\\config" "$INSTDIR\\VSTPlugins"', SETUP_SOURCE
        )
        self.assertIn('$InstallRollbackCopyCode >= 8', SETUP_SOURCE)
        self.assertLess(SETUP_SOURCE.index(prepare_call), SETUP_SOURCE.index(install_new_dll))

        failure_index = SETUP_SOURCE.index(failed_registration)
        restore_index = SETUP_SOURCE.index(restore_call, failure_index)
        abort_index = SETUP_SOURCE.index("Abort", restore_index)
        commit_index = SETUP_SOURCE.index(commit_call, abort_index)
        self.assertLess(failure_index, restore_index)
        self.assertLess(restore_index, abort_index)
        self.assertLess(abort_index, commit_index)

        self.assertIn("Function RemoveInstalledProductFiles", SETUP_SOURCE)
        self.assertIn(
            '!insertmacro DeleteTransactionFile '
            '"$INSTDIR\\EqualizerAPO.dll"',
            SETUP_SOURCE,
        )
        self.assertIn(
            '!insertmacro DeleteTransactionFile '
            '"$INSTDIR\\qt\\platforms\\qwindows.dll"',
            SETUP_SOURCE,
        )
        remove_start = SETUP_SOURCE.index("Function RemoveInstalledProductFiles")
        remove_end = SETUP_SOURCE.index("FunctionEnd", remove_start)
        remove_body = SETUP_SOURCE[remove_start:remove_end]
        self.assertNotIn('RMDir /r "$INSTDIR\\qt"', remove_body)
        self.assertIn(
            '"$SYSDIR\\robocopy.exe" "$InstallRollbackFiles" '
            '"$INSTDIR" /E /COPY:DATS',
            SETUP_SOURCE,
        )
        self.assertGreaterEqual(
            SETUP_SOURCE.count(
                'ExecWait \'"$SYSDIR\\regsvr32.exe" /s '
                '"$INSTDIR\\EqualizerAPO.dll"\''
            ),
            2,
        )
        self.assertIn(
            'ExecWait \'"$SYSDIR\\regsvr32.exe" /u /s '
            '"$INSTDIR\\EqualizerAPO.dll"\'',
            SETUP_SOURCE,
        )
        self.assertIn("Function .onInstFailed", SETUP_SOURCE)
        self.assertIn('$NewApoRegistrationAttempted == "1"', SETUP_SOURCE)
        self.assertIn(
            'StrCpy $NewApoRegistrationAttempted "1"', SETUP_SOURCE
        )

        rollback_start = SETUP_SOURCE.index("Function RollbackInstallTransaction")
        rollback_end = SETUP_SOURCE.index("FunctionEnd", rollback_start)
        rollback_body = SETUP_SOURCE[rollback_start:rollback_end]
        unregister = rollback_body.index(
            'ExecWait \'"$SYSDIR\\regsvr32.exe" /u /s '
            '"$INSTDIR\\EqualizerAPO.dll"\''
        )
        phase_guard = rollback_body.index('$NewApoRegistrationAttempted == "1"')
        self.assertLess(phase_guard, unregister)
        self.assertIn("ClearErrors", rollback_body[:unregister])
        self.assertIn("${Errors}", rollback_body[unregister:])
        reregister = rollback_body.index(
            'ExecWait \'"$SYSDIR\\regsvr32.exe" /s '
            '"$INSTDIR\\EqualizerAPO.dll"\' '
            "$ApoRollbackRegistrationCode"
        )
        self.assertLess(
            rollback_body.rindex("ClearErrors", 0, reregister), reregister
        )
        self.assertLess(
            rollback_body.rindex(
                'StrCpy $ApoRollbackRegistrationCode '
                '"process did not start"',
                0,
                reregister,
            ),
            reregister,
        )
        self.assertGreater(
            rollback_body.index("${If} ${Errors}", reregister), reregister
        )
        self.assertIn(
            "Recovery files remain at $InstallRollbackDirectory", rollback_body
        )
        # State and durable snapshot are cleared only on the single success tail.
        self.assertEqual(
            rollback_body.count('StrCpy $InstallRollbackState "0"'), 1
        )
        self.assertLess(
            rollback_body.rindex("Call DiscardRenamedProductFiles"),
            rollback_body.rindex('StrCpy $InstallRollbackState "0"'),
        )

        self.assertNotIn("$INSTDIR\\config", remove_body)
        self.assertNotIn("$INSTDIR\\VSTPlugins", remove_body)

    def test_registration_is_the_persistent_commit_point(self) -> None:
        install_start = SETUP_SOURCE.index('Section "-Install"')
        install_end = SETUP_SOURCE.index("SectionEnd", install_start)
        install_body = SETUP_SOURCE[install_start:install_end]
        registration = install_body.index(
            'ExecWait \'"$SYSDIR\\regsvr32.exe" /s '
            '"$INSTDIR\\EqualizerAPO.dll"\' $1'
        )
        commit = install_body.index("Call CommitInstallTransaction", registration)

        persistent_operations = (
            'nsExec::ExecToLog \'"$SYSDIR\\icacls.exe"',
            'WriteRegStr HKLM ${REGPATH} "InstallPath"',
            'WriteUninstaller "$INSTDIR\\Uninstall.exe"',
            "!insertmacro DeleteProductShortcuts $OldStartMenuFolder",
            '!insertmacro MUI_STARTMENU_WRITE_BEGIN Application',
            'WriteRegStr HKLM ${UNINST_REGPATH} "DisplayName"',
            'DeviceSelector.exe" /r /s',
        )
        for operation in persistent_operations:
            with self.subTest(operation=operation):
                self.assertGreater(install_body.index(operation), commit)

        verification = install_body.index("Call VerifyRequiredAssets")
        self.assertLess(verification, registration)
        missing_asset = SETUP_SOURCE.index("missingRequiredAsset:")
        rollback_after_missing = SETUP_SOURCE.index(
            "Call RollbackInstallTransaction", missing_asset
        )
        abort_after_missing = SETUP_SOURCE.index("Abort", rollback_after_missing)
        self.assertLess(rollback_after_missing, abort_after_missing)

    def test_old_file_cleanup_is_scoped_to_product_files(self) -> None:
        self.assertIn("Function DiscardRenamedProductFiles", SETUP_SOURCE)
        self.assertIn("Call DiscardRenamedProductFiles", SETUP_SOURCE)
        self.assertIn(
            'FileWrite $RenameManifestHandle "$renamePath$\\r$\\n"',
            SETUP_SOURCE,
        )
        self.assertIn('Delete /REBOOTOK "$1"', SETUP_SOURCE)
        self.assertNotIn('Delete /REBOOTOK "$INSTDIR\\*.old"', SETUP_SOURCE)
        self.assertNotIn('Delete /REBOOTOK "$INSTDIR\\*.old.*"', SETUP_SOURCE)
        self.assertNotIn('Delete "${path}.old', SETUP_SOURCE)

    def test_regsvr32_process_creation_errors_cannot_commit(self) -> None:
        install_start = SETUP_SOURCE.index('Section "-Install"')
        install_end = SETUP_SOURCE.index("SectionEnd", install_start)
        install_body = SETUP_SOURCE[install_start:install_end]
        registration = install_body.index(
            'ExecWait \'"$SYSDIR\\regsvr32.exe" /s '
            '"$INSTDIR\\EqualizerAPO.dll"\' $1'
        )
        clear_errors = install_body.rindex("ClearErrors", 0, registration)
        sentinel = install_body.rindex(
            'StrCpy $1 "process did not start"', 0, registration
        )
        error_check = install_body.index("${If} ${Errors}", registration)
        failure_jump = install_body.index(
            "Goto apoRegistrationFailed", error_check
        )
        commit = install_body.index("Call CommitInstallTransaction", registration)
        self.assertLess(clear_errors, registration)
        self.assertLess(sentinel, registration)
        self.assertLess(registration, error_check)
        self.assertLess(error_check, failure_jump)
        self.assertLess(failure_jump, commit)

    def test_protected_audio_override_has_a_next_run_recovery_journal(self) -> None:
        self.assertIn(
            '!define INSTALLER_RECOVERY_REGPATH '
            '"${REGPATH}\\InstallerRecovery"',
            SETUP_SOURCE,
        )
        init_start = SETUP_SOURCE.index("Function .onInit")
        init_end = SETUP_SOURCE.index("FunctionEnd", init_start)
        init_body = SETUP_SOURCE[init_start:init_end]
        self.assertIn("Call RecoverProtectedAudioSetting", init_body)

        begin_start = SETUP_SOURCE.index("Function BeginProtectedAudioOverride")
        begin_end = SETUP_SOURCE.index("FunctionEnd", begin_start)
        begin_body = SETUP_SOURCE[begin_start:begin_end]
        pending = begin_body.index(
            'WriteRegDWORD HKLM ${INSTALLER_RECOVERY_REGPATH} "Pending" 1'
        )
        override = begin_body.index(
            'WriteRegDWORD HKLM '
            '"Software\\Microsoft\\Windows\\CurrentVersion\\Audio" '
            '"DisableProtectedAudioDG" 1'
        )
        self.assertLess(pending, override)

        recover_start = SETUP_SOURCE.index(
            "Function RecoverProtectedAudioSetting"
        )
        recover_end = SETUP_SOURCE.index("FunctionEnd", recover_start)
        recover_body = SETUP_SOURCE[recover_start:recover_end]
        self.assertIn(
            'ReadRegDWORD $0 HKLM ${INSTALLER_RECOVERY_REGPATH} "Pending"',
            recover_body,
        )
        self.assertIn("Call RestoreProtectedAudioSetting", recover_body)
        restore_start = SETUP_SOURCE.index("Function RestoreProtectedAudioSetting")
        restore_end = SETUP_SOURCE.index("FunctionEnd", restore_start)
        restore_body = SETUP_SOURCE[restore_start:restore_end]
        self.assertIn(
            'DeleteRegValue HKLM '
            '"Software\\Microsoft\\Windows\\CurrentVersion\\Audio" '
            '"DisableProtectedAudioDG"',
            restore_body,
        )
        self.assertIn(
            'StrCpy $ProtectedAudioOverrideActive "1"', restore_body
        )
        self.assertIn(
            'StrCpy $ProtectedAudioOverrideActive "0"', restore_body
        )
        self.assertLess(
            restore_body.index(
                'ReadRegDWORD $0 HKLM ${INSTALLER_RECOVERY_REGPATH} "Pending"'
            ),
            restore_body.index('StrCpy $ProtectedAudioOverrideActive "0"'),
        )

    def test_start_menu_cleanup_preserves_unrelated_shortcuts(self) -> None:
        self.assertIn("!macro DeleteProductShortcuts folder", SETUP_SOURCE)
        self.assertIn('CreateShortCut "$SMPROGRAMS\\$StartMenuFolder\\Check for updates.lnk"', SETUP_SOURCE)
        self.assertNotIn('RMDir /r "$SMPROGRAMS\\$OldStartMenuFolder"', SETUP_SOURCE)
        self.assertNotIn('RMDir /r "$SMPROGRAMS\\$StartMenuFolder"', SETUP_SOURCE)


if __name__ == "__main__":
    unittest.main()
