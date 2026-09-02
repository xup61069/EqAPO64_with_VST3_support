#!/usr/bin/env python3
"""Regression contracts for unattended installer behavior."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SETUP_SOURCE = (ROOT / "Setup" / "Setup.nsi").read_text(encoding="utf-8")
SETUP64_SOURCE = (ROOT / "Setup" / "Setup64.nsi").read_text(encoding="utf-8")
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
        self.assertIn(
            'StrCpy $StartMenuFolder "${PRODUCT_LABEL} ${VERSION}"',
            SETUP_SOURCE,
        )

    def test_user_facing_identity_is_an_unofficial_loudness_correction_fork(self) -> None:
        self.assertIn(
            '!define PRODUCT_LABEL "Loudness Correction for Equalizer APO"',
            SETUP_SOURCE,
        )
        self.assertIn(
            '!define PRODUCT_FULL_LABEL '
            '"${PRODUCT_LABEL} (unofficial fork)"',
            SETUP_SOURCE,
        )
        self.assertIn('Name "${PRODUCT_FULL_LABEL} ${VERSION}"', SETUP_SOURCE)
        self.assertIn(
            'VIAddVersionKey /LANG=1033 "ProductName" '
            '"${PRODUCT_FULL_LABEL}"',
            SETUP_SOURCE,
        )
        self.assertIn(
            '"DisplayName" "${PRODUCT_FULL_LABEL}"', SETUP_SOURCE
        )
        self.assertNotIn('Name "Equalizer APO ${VERSION}"', SETUP_SOURCE)

        # These identifiers and tool names are compatibility contracts, not a
        # claim that this fork is the upstream product.
        self.assertIn('!define REGPATH "Software\\EqualizerAPO"', SETUP_SOURCE)
        self.assertIn(
            '!define UNINST_REGPATH '
            '"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\EqualizerAPO"',
            SETUP_SOURCE,
        )
        self.assertIn(
            'StrCpy $INSTDIR "$PROGRAMFILES64\\EqualizerAPO"', SETUP_SOURCE
        )
        self.assertIn("EqualizerAPOUpdateChecker", SETUP_SOURCE)
        self.assertIn("Equalizer APO Configuration Editor.lnk", SETUP_SOURCE)
        self.assertIn("Equalizer APO Device Selector.lnk", SETUP_SOURCE)
        self.assertIn(
            'OutFile "EqualizerAPO-x64-${VERSION}.exe"', SETUP64_SOURCE
        )

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
            SETUP_SOURCE.index(
                '!insertmacro WriteRequiredRegStr HKLM "${REGPATH}" '
                '"InstallPath"'
            ),
        )
        self.assertIn("Function SaveProtectedAudioSetting", SETUP_SOURCE)
        self.assertIn("Function RestoreProtectedAudioSetting", SETUP_SOURCE)

    def test_failed_registration_rolls_back_the_application_tree(self) -> None:
        prepare_call = "Call PrepareInstallTransaction"
        install_new_dll = 'File "${BINPATH}\\EqualizerAPO.dll"'
        restore_call = "Call RollbackInstallTransaction"
        commit_call = "Call CommitInstallTransaction"

        self.assertNotIn("GetTempFileName $InstallRollbackDirectory", SETUP_SOURCE)
        self.assertNotIn("$PLUGINSDIR\\install-rollback", SETUP_SOURCE)
        self.assertIn(
            'StrCpy $InstallRecoveryProductRoot '
            '"$InstallRecoveryCommonAppData\\EqualizerAPO"',
            SETUP_SOURCE,
        )
        self.assertIn(
            'StrCpy $InstallRollbackDirectory '
            '"$InstallRecoveryProductRoot\\InstallerRecovery"',
            SETUP_SOURCE,
        )
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

        install_start = SETUP_SOURCE.index('Section "-Install"')
        install_end = SETUP_SOURCE.index("SectionEnd", install_start)
        install_body = SETUP_SOURCE[install_start:install_end]
        registration_index = install_body.index(
            'ExecWait \'"$SYSDIR\\regsvr32.exe" /s '
            '"$INSTDIR\\EqualizerAPO.dll"\' $1'
        )
        failure_index = install_body.index("${If} $1 != 0", registration_index)
        restore_index = install_body.index(restore_call, failure_index)
        abort_index = install_body.index("Abort", restore_index)
        commit_index = install_body.index(commit_call, abort_index)
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
        # The rollback decision is made durable first. Rename/snapshot cleanup is
        # restartable, and the journal is always the final deletion.
        self.assertEqual(
            rollback_body.count('StrCpy $InstallRollbackState "0"'), 2
        )
        rollback_phase = rollback_body.rindex('"Phase" "rollback-cleanup"')
        rollback_state = rollback_body.rindex('StrCpy $InstallRollbackState "0"')
        renamed_cleanup = rollback_body.rindex("Call DiscardRenamedProductFiles")
        snapshot_cleanup = rollback_body.rindex("Call DiscardInstallRecoverySnapshot")
        journal_cleanup = rollback_body.rindex("Call ClearInstallRecoveryJournal")
        self.assertLess(rollback_phase, rollback_state)
        self.assertLess(rollback_state, renamed_cleanup)
        self.assertLess(renamed_cleanup, snapshot_cleanup)
        self.assertLess(snapshot_cleanup, journal_cleanup)

        self.assertNotIn("$INSTDIR\\config", remove_body)
        self.assertNotIn("$INSTDIR\\VSTPlugins", remove_body)

    def test_commit_follows_every_fallible_persistent_operation(self) -> None:
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
            '!insertmacro WriteRequiredRegStr HKLM "${REGPATH}" "InstallPath"',
            'WriteUninstaller "$INSTDIR\\Uninstall.exe"',
            "!insertmacro DeleteProductShortcuts $OldStartMenuFolder",
            '!insertmacro MUI_STARTMENU_WRITE_BEGIN Application',
            '!insertmacro WriteRequiredRegStr HKLM "${UNINST_REGPATH}" "DisplayName"',
            'DeviceSelector.exe" /r /s',
            'UpdateChecker.exe" -i -s',
            'UpdateChecker.exe" -u -s',
        )
        for operation in persistent_operations:
            with self.subTest(operation=operation):
                self.assertLess(install_body.index(operation), commit)

        failure_label = install_body.index("installTransactionFailed:")
        rollback = install_body.index("Call RollbackInstallTransaction", failure_label)
        abort = install_body.index("Abort", rollback)
        self.assertLess(commit, failure_label)
        self.assertLess(failure_label, rollback)
        self.assertLess(rollback, abort)

        commit_start = SETUP_SOURCE.index("Function CommitInstallTransaction")
        commit_end = SETUP_SOURCE.index("FunctionEnd", commit_start)
        commit_body = SETUP_SOURCE[commit_start:commit_end]
        self.assertIn(
            'WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} '
            '"Phase" "committed"',
            commit_body,
        )
        self.assertIn(
            'ReadRegStr $InstallRecoveryPhase HKLM '
            '${INSTALLER_APP_RECOVERY_REGPATH} "Phase"',
            commit_body,
        )

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
        macro_start = SETUP_SOURCE.index("!macro RenameAndDelete path")
        macro_end = SETUP_SOURCE.index("!macroend", macro_start)
        macro_body = SETUP_SOURCE[macro_start:macro_end]
        self.assertLess(
            macro_body.index('FileWrite $RenameManifestHandle "$renamePath$\\r$\\n"'),
            macro_body.index('Rename "${path}" "$renamePath"'),
        )

        cleanup_start = SETUP_SOURCE.index("Function DiscardRenamedProductFiles")
        cleanup_end = SETUP_SOURCE.index("FunctionEnd", cleanup_start)
        cleanup_body = SETUP_SOURCE[cleanup_start:cleanup_end]
        self.assertIn('GetFullPathName $5 "$INSTDIR"', cleanup_body)
        self.assertIn('GetFullPathName $4 "$1"', cleanup_body)
        self.assertIn('StrCpy $6 "$5\\"', cleanup_body)
        self.assertIn('StrCpy $8 "$4" $7', cleanup_body)
        self.assertIn('${If} $8 == "$6"', cleanup_body)
        self.assertIn('Delete /REBOOTOK "$4"', cleanup_body)
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", cleanup_body)
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
        persistent_attempt = install_body.index(
            'WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} '
            '"NewApoRegistrationAttempted" 1'
        )
        self.assertLess(persistent_attempt, registration)
        self.assertLess(clear_errors, registration)
        self.assertLess(sentinel, registration)
        self.assertLess(registration, error_check)
        self.assertLess(error_check, failure_jump)
        self.assertLess(failure_jump, commit)

    def test_app_tree_recovery_journal_survives_process_and_power_loss(self) -> None:
        self.assertIn(
            '!define INSTALLER_APP_RECOVERY_REGPATH '
            '"${REGPATH}\\InstallerAppRecovery"',
            SETUP_SOURCE,
        )
        self.assertIn(
            "!define CSIDL_COMMON_APPDATA 0x23",
            SETUP_SOURCE,
        )
        self.assertIn("shell32::SHGetFolderPathW", SETUP_SOURCE)
        self.assertIn(
            'StrCpy $InstallRollbackDirectory '
            '"$InstallRecoveryProductRoot\\InstallerRecovery"',
            SETUP_SOURCE,
        )
        self.assertNotIn("GetTempFileName", SETUP_SOURCE)
        self.assertNotIn('RMDir /r "$INSTDIR"', SETUP_SOURCE)
        self.assertNotIn(
            'ReadRegStr $InstallRollbackDirectory HKLM', SETUP_SOURCE
        )

        init_start = SETUP_SOURCE.index("Function .onInit")
        init_end = SETUP_SOURCE.index("FunctionEnd", init_start)
        init_body = SETUP_SOURCE[init_start:init_end]
        protected = init_body.index("Call RecoverProtectedAudioSetting")
        app_tree = init_body.index("Call RecoverInstallTransaction")
        normal_state = init_body.index("!insertmacro MUI_LANGDLL_DISPLAY")
        self.assertLess(protected, app_tree)
        self.assertLess(app_tree, normal_state)

        prepare_start = SETUP_SOURCE.index("Function PrepareInstallTransaction")
        prepare_end = SETUP_SOURCE.index("FunctionEnd", prepare_start)
        prepare_body = SETUP_SOURCE[prepare_start:prepare_end]
        initializing = prepare_body.index('"Phase" "initializing"')
        pending = prepare_body.index('"Pending" 1', initializing)
        secure_tree = prepare_body.index("Call CreateSecureInstallRecoveryTree", pending)
        preparing = prepare_body.index('"Phase" "preparing"')
        snapshot = prepare_body.index('"$SYSDIR\\robocopy.exe"', pending)
        marker = prepare_body.index("EqualizerAPO installer app-tree recovery v1")
        prepared = prepare_body.index('"Phase" "prepared"', marker)
        active = prepare_body.index('"Phase" "active"', prepared)
        self.assertLess(initializing, pending)
        self.assertLess(pending, secure_tree)
        self.assertLess(secure_tree, preparing)
        self.assertLess(preparing, snapshot)
        self.assertLess(snapshot, marker)
        self.assertLess(marker, prepared)
        self.assertLess(prepared, active)

        recover_start = SETUP_SOURCE.index("Function RecoverInstallTransaction")
        recover_end = SETUP_SOURCE.index("FunctionEnd", recover_start)
        recover_body = SETUP_SOURCE[recover_start:recover_end]
        for phase in (
            "initializing",
            "preparing",
            "prepared",
            "committed",
            "rollback-cleanup",
        ):
            with self.subTest(phase=phase):
                self.assertIn(f'$InstallRecoveryPhase == "{phase}"', recover_body)
        self.assertIn('$InstallRecoveryPhase != "active"', recover_body)
        self.assertIn("Call ValidateActiveInstallRecoverySnapshot", recover_body)
        self.assertIn("Call LoadInstallRecoveryTargetFromJournal", recover_body)
        self.assertIn("Call RollbackInstallTransaction", recover_body)
        discard_start = SETUP_SOURCE.index("Function DiscardInstallRecoverySnapshot")
        discard_end = SETUP_SOURCE.index("FunctionEnd", discard_start)
        discard_body = SETUP_SOURCE[discard_start:discard_end]
        self.assertLess(
            discard_body.index("Call InitializeInstallRecoveryPaths"),
            discard_body.index("Call SecureExistingInstallRecoveryTree"),
        )
        self.assertLess(
            discard_body.index("Call SecureExistingInstallRecoveryTree"),
            discard_body.index('RMDir /r "$InstallRollbackDirectory"'),
        )

    def test_recovery_tree_rejects_reparse_points_and_is_created_with_a_secure_acl(
        self,
    ) -> None:
        self.assertIn("!define FILE_ATTRIBUTE_DIRECTORY 0x10", SETUP_SOURCE)
        self.assertIn("!define FILE_ATTRIBUTE_REPARSE_POINT 0x400", SETUP_SOURCE)

        validate_start = SETUP_SOURCE.index(
            "Function ValidateInstallRecoveryComponent"
        )
        validate_end = SETUP_SOURCE.index("FunctionEnd", validate_start)
        validate_body = SETUP_SOURCE[validate_start:validate_end]
        self.assertIn("kernel32::GetFileAttributesW", validate_body)
        self.assertIn("ERROR_FILE_NOT_FOUND", validate_body)
        self.assertIn("ERROR_PATH_NOT_FOUND", validate_body)
        self.assertIn("FILE_ATTRIBUTE_DIRECTORY", validate_body)
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", validate_body)

        components_start = SETUP_SOURCE.index(
            "Function ValidateInstallRecoveryComponents"
        )
        components_end = SETUP_SOURCE.index("FunctionEnd", components_start)
        components_body = SETUP_SOURCE[components_start:components_end]
        component_paths = (
            "$InstallRecoveryCommonAppData",
            "$InstallRecoveryProductRoot",
            "$InstallRollbackDirectory",
            "$InstallRollbackFiles",
        )
        last_path = -1
        for path in component_paths:
            with self.subTest(path=path):
                path_index = components_body.index(
                    f'StrCpy $InstallRecoveryPathToCheck "{path}"'
                )
                self.assertGreater(path_index, last_path)
                last_path = path_index

        atomic_start = SETUP_SOURCE.index(
            "Function CreateInstallRecoveryDirectoryAtomically"
        )
        atomic_end = SETUP_SOURCE.index("FunctionEnd", atomic_start)
        atomic_body = SETUP_SOURCE[atomic_start:atomic_end]
        protected_sddl = "O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"
        self.assertIn(protected_sddl, atomic_body)
        self.assertIn("*(i 12, p r0, i 0) p .r2", atomic_body)
        self.assertIn('${If} $2 == 0', atomic_body)
        self.assertIn("kernel32::CreateDirectoryW", atomic_body)
        self.assertLess(
            atomic_body.index("ConvertStringSecurityDescriptorToSecurityDescriptorW"),
            atomic_body.index("kernel32::CreateDirectoryW"),
        )
        self.assertLess(
            atomic_body.index("kernel32::CreateDirectoryW"),
            atomic_body.index("Call ValidateInstallRecoveryComponent"),
        )

        create_start = SETUP_SOURCE.index("Function CreateSecureInstallRecoveryTree")
        create_end = SETUP_SOURCE.index("FunctionEnd", create_start)
        create_body = SETUP_SOURCE[create_start:create_end]
        root_create = create_body.index(
            'StrCpy $InstallRecoveryAclTarget "$InstallRecoveryProductRoot"'
        )
        ownership_write = create_body.index('"ProductRootCreated" 1', root_create)
        ownership_read = create_body.index('"ProductRootCreated"', ownership_write + 1)
        child_create = create_body.index(
            'StrCpy $InstallRecoveryAclTarget "$InstallRollbackDirectory"',
            ownership_read,
        )
        files_create = create_body.index(
            'StrCpy $InstallRecoveryAclTarget "$InstallRollbackFiles"', child_create
        )
        self.assertLess(root_create, ownership_write)
        self.assertLess(ownership_write, ownership_read)
        self.assertLess(ownership_read, child_create)
        self.assertLess(child_create, files_create)

        secure_start = SETUP_SOURCE.index(
            "Function SecureExistingInstallRecoveryTree"
        )
        secure_end = SETUP_SOURCE.index("FunctionEnd", secure_start)
        secure_body = SETUP_SOURCE[secure_start:secure_end]
        self.assertIn('ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ProductRootCreated"', secure_body)
        self.assertIn('${If} $0 == 0', secure_body)
        self.assertIn('${ElseIf} $InstallRecoveryPathExists == "0"', secure_body)
        self.assertIn("without a verified ownership marker", secure_body)
        self.assertIn("Call HardenInstallRecoveryDirectory", secure_body)

        discard_start = SETUP_SOURCE.index("Function DiscardInstallRecoverySnapshot")
        discard_end = SETUP_SOURCE.index("FunctionEnd", discard_start)
        discard_body = SETUP_SOURCE[discard_start:discard_end]
        recursive_delete = discard_body.index(
            'RMDir /r "$InstallRollbackDirectory"'
        )
        self.assertLess(
            discard_body.index("Call SecureExistingInstallRecoveryTree"),
            recursive_delete,
        )
        self.assertLess(
            discard_body.rindex("Call ValidateInstallRecoveryComponents", 0, recursive_delete),
            recursive_delete,
        )
        self.assertEqual(
            SETUP_SOURCE.count('RMDir /r "$InstallRollbackDirectory"'), 1
        )

        active_start = SETUP_SOURCE.index(
            "Function ValidateActiveInstallRecoverySnapshot"
        )
        active_end = SETUP_SOURCE.index("FunctionEnd", active_start)
        active_body = SETUP_SOURCE[active_start:active_end]
        self.assertIn('"ProductRootCreated"', active_body)
        for required_path in component_paths:
            with self.subTest(required_active_path=required_path):
                path_index = active_body.index(
                    f'StrCpy $InstallRecoveryPathToCheck "{required_path}"'
                )
                required_index = active_body.index(
                    'StrCpy $InstallRecoveryPathRequired "1"', path_index
                )
                self.assertLess(path_index, required_index)
        self.assertIn(
            'GetFileAttributesW(w "$InstallRecoveryMarkerPath")', active_body
        )
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", active_body)
        self.assertIn("EqualizerAPO installer app-tree recovery v1", active_body)

    def test_recovery_path_overlap_and_manifest_containment_are_canonical(self) -> None:
        resolver_start = SETUP_SOURCE.index(
            "Function ResolveInstallRecoveryPhysicalPath"
        )
        resolver_end = SETUP_SOURCE.index("FunctionEnd", resolver_start)
        resolver_body = SETUP_SOURCE[resolver_start:resolver_end]
        self.assertIn("kernel32::CreateFileW", resolver_body)
        self.assertIn("FILE_FLAG_BACKUP_SEMANTICS", resolver_body)
        self.assertIn("FILE_SHARE_READ_WRITE_DELETE", resolver_body)
        self.assertIn("kernel32::GetFinalPathNameByHandleW", resolver_body)
        self.assertIn("kernel32::CloseHandle", resolver_body)
        self.assertIn('\\\\?\\UNC\\', resolver_body)
        self.assertIn('\\\\?\\', resolver_body)

        target_start = SETUP_SOURCE.index("Function ValidateInstallRecoveryTarget")
        target_end = SETUP_SOURCE.index("FunctionEnd", target_start)
        target_body = SETUP_SOURCE[target_start:target_end]
        self.assertGreaterEqual(target_body.count("GetFullPathName"), 2)
        self.assertIn("normalizeInstallPathTail:", target_body)
        self.assertIn("normalizeRecoveryPathTail:", target_body)
        self.assertIn('StrCpy $2 "$INSTDIR\\"', target_body)
        self.assertIn('StrCpy $3 "$InstallRollbackDirectory\\"', target_body)
        self.assertIn('StrCpy $5 "$3" $4', target_body)
        self.assertIn('StrCpy $5 "$2" $4', target_body)
        self.assertGreaterEqual(
            target_body.count("Call ResolveInstallRecoveryPhysicalPath"), 2
        )
        self.assertIn("$InstallRecoveryPhysicalInstallPath", target_body)
        self.assertIn(
            "$InstallRecoveryPhysicalPath\\EqualizerAPO\\InstallerRecovery\\",
            target_body,
        )
        self.assertIn('"PhysicalInstallPath"', target_body)

        metadata_start = SETUP_SOURCE.index("Function SaveInstallMetadataJournal")
        metadata_end = SETUP_SOURCE.index("FunctionEnd", metadata_start)
        metadata_body = SETUP_SOURCE[metadata_start:metadata_end]
        self.assertIn(
            '"PhysicalInstallPath" "$InstallRecoveryPhysicalInstallPath"',
            metadata_body,
        )

        load_start = SETUP_SOURCE.index(
            "Function LoadInstallRecoveryTargetFromJournal"
        )
        load_end = SETUP_SOURCE.index("FunctionEnd", load_start)
        load_body = SETUP_SOURCE[load_start:load_end]
        self.assertIn('ReadRegStr $0 HKLM', load_body)
        self.assertIn('"PhysicalInstallPath"', load_body)
        self.assertIn('${OrIf} $0 == ""', load_body)
        self.assertIn(
            '${OrIf} $0 != "$InstallRecoveryPhysicalInstallPath"', load_body
        )

        def overlaps(left: str, right: str) -> bool:
            left_delimited = left.rstrip("\\").casefold() + "\\"
            right_delimited = right.rstrip("\\").casefold() + "\\"
            return left_delimited.startswith(right_delimited) or right_delimited.startswith(
                left_delimited
            )

        recovery = r"C:\ProgramData\EqualizerAPO\InstallerRecovery"
        self.assertTrue(overlaps(recovery, recovery))
        self.assertTrue(overlaps(r"C:\ProgramData\EqualizerAPO", recovery))
        self.assertTrue(overlaps(recovery + r"\files", recovery))
        self.assertFalse(
            overlaps(r"C:\ProgramData\EqualizerAPO\InstallerRecovery2", recovery)
        )

        cleanup_start = SETUP_SOURCE.index("Function DiscardRenamedProductFiles")
        cleanup_end = SETUP_SOURCE.index("FunctionEnd", cleanup_start)
        cleanup_body = SETUP_SOURCE[cleanup_start:cleanup_end]
        canonical_entry = cleanup_body.index('GetFullPathName $4 "$1"')
        containment = cleanup_body.index('${If} $8 == "$6"', canonical_entry)
        deletion = cleanup_body.index('Delete /REBOOTOK "$4"', containment)
        self.assertLess(canonical_entry, containment)
        self.assertLess(containment, deletion)

    def test_updater_task_is_snapshotted_and_restored_from_exact_xml(self) -> None:
        self.assertIn(
            'StrCpy $InstallRecoveryTaskXmlPath '
            '"$InstallRollbackDirectory\\update-task.xml"',
            SETUP_SOURCE,
        )
        prepare_start = SETUP_SOURCE.index("Function PrepareInstallTransaction")
        prepare_end = SETUP_SOURCE.index("FunctionEnd", prepare_start)
        prepare_body = SETUP_SOURCE[prepare_start:prepare_end]
        task_query = prepare_body.index(
            'schtasks.exe" /Query /TN "EqualizerAPOUpdateChecker" /FO LIST'
        )
        xml_export = prepare_body.index(
            r'/Query /TN $\"EqualizerAPOUpdateChecker$\" /XML > '
            r'$\"$InstallRecoveryTaskXmlPath$\"',
            task_query,
        )
        xml_saved = prepare_body.index('"PreviousUpdateTaskXmlSaved" 1', xml_export)
        self.assertLess(task_query, xml_export)
        self.assertLess(xml_export, xml_saved)
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", prepare_body[xml_export:xml_saved])
        self.assertIn('FileOpen $0 "$InstallRecoveryTaskXmlPath" r', prepare_body)
        self.assertIn('${If} $1 == ""', prepare_body)
        self.assertIn('${ElseIf} $InstallOperationCode == 1', prepare_body)
        self.assertIn(
            '$WINDIR\\System32\\Tasks\\EqualizerAPOUpdateChecker', prepare_body
        )
        self.assertIn('"PreviousUpdateTaskPresent" 0', prepare_body)

        rollback_start = SETUP_SOURCE.index("Function RollbackInstallTransaction")
        rollback_end = SETUP_SOURCE.index("FunctionEnd", rollback_start)
        rollback_body = SETUP_SOURCE[rollback_start:rollback_end]
        self.assertIn(
            'schtasks.exe" /Create /TN "EqualizerAPOUpdateChecker" '
            '/XML "$InstallRecoveryTaskXmlPath" /F',
            rollback_body,
        )
        self.assertIn(
            'schtasks.exe" /Delete /TN "EqualizerAPOUpdateChecker" /F',
            rollback_body,
        )
        self.assertIn('"PreviousUpdateTaskXmlSaved"', rollback_body)
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", rollback_body)
        self.assertIn(
            '$WINDIR\\System32\\Tasks\\EqualizerAPOUpdateChecker', rollback_body
        )
        self.assertIn('${If} $InstallOperationCode == "error"', rollback_body)
        self.assertIn('${ElseIf} $InstallOperationCode != 0', rollback_body)
        self.assertNotIn("UpdateChecker.exe", rollback_body)
        self.assertLess(
            rollback_body.index('/Create /TN "EqualizerAPOUpdateChecker"'),
            rollback_body.index('"UpdaterOperationStarted" 0'),
        )

    def test_interactive_endpoint_selection_is_post_commit_only(self) -> None:
        install_start = SETUP_SOURCE.index('Section "-Install"')
        install_end = SETUP_SOURCE.index("SectionEnd", install_start)
        install_body = SETUP_SOURCE[install_start:install_end]
        silent_restart = install_body.index('DeviceSelector.exe" /r /s')
        commit = install_body.index("Call CommitInstallTransaction", silent_restart)
        interactive = install_body.index('DeviceSelector.exe" /i', commit)
        complete = install_body.index("Goto installTransactionComplete", interactive)
        self.assertLess(silent_restart, commit)
        self.assertLess(commit, interactive)
        self.assertLess(interactive, complete)
        self.assertNotIn('DeviceSelector.exe" /i', install_body[:commit])
        post_commit_action = install_body[interactive:complete]
        self.assertNotIn("Call RollbackInstallTransaction", post_commit_action)
        self.assertNotIn("Goto installTransactionFailed", post_commit_action)
        self.assertIn("No committed files were rolled back", post_commit_action)
        self.assertNotIn("SetRebootFlag false", SETUP_SOURCE)

    def test_transaction_cleanup_is_restartable_and_journal_last(self) -> None:
        cleanup_start = SETUP_SOURCE.index(
            "Function CleanupCompletedInstallTransaction"
        )
        cleanup_end = SETUP_SOURCE.index("FunctionEnd", cleanup_start)
        cleanup_body = SETUP_SOURCE[cleanup_start:cleanup_end]
        target = cleanup_body.index("Call LoadInstallRecoveryTargetFromJournal")
        renamed = cleanup_body.index("Call DiscardRenamedProductFiles", target)
        snapshot = cleanup_body.index("Call DiscardInstallRecoverySnapshot", renamed)
        journal = cleanup_body.index("Call ClearInstallRecoveryJournal", snapshot)
        self.assertLess(target, renamed)
        self.assertLess(renamed, snapshot)
        self.assertLess(snapshot, journal)

        recover_start = SETUP_SOURCE.index("Function RecoverInstallTransaction")
        recover_end = SETUP_SOURCE.index("FunctionEnd", recover_start)
        recover_body = SETUP_SOURCE[recover_start:recover_end]
        no_pending_start = recover_body.index(
            "; A fixed name is not proof of ownership."
        )
        no_pending_end = recover_body.index(
            'ReadRegStr $InstallRecoveryPhase', no_pending_start
        )
        no_pending_body = recover_body[no_pending_start:no_pending_end]
        self.assertIn("unjournaled installer recovery tree", no_pending_body)
        self.assertNotIn("Call DiscardInstallRecoverySnapshot", no_pending_body)
        self.assertIn(
            '${ElseIf} $InstallRecoveryPhase == "committed"', recover_body
        )
        self.assertIn(
            '${ElseIf} $InstallRecoveryPhase == "rollback-cleanup"', recover_body
        )

        prepare_start = SETUP_SOURCE.index("Function PrepareInstallTransaction")
        prepare_end = SETUP_SOURCE.index("FunctionEnd", prepare_start)
        prepare_body = SETUP_SOURCE[prepare_start:prepare_end]
        failed_tail = prepare_body[prepare_body.index("installBackupFailed:") :]
        self.assertLess(
            failed_tail.index("Call DiscardInstallRecoverySnapshot"),
            failed_tail.index("Call ClearInstallRecoveryJournal"),
        )

        commit_start = SETUP_SOURCE.index("Function CommitInstallTransaction")
        commit_end = SETUP_SOURCE.index("FunctionEnd", commit_start)
        commit_body = SETUP_SOURCE[commit_start:commit_end]
        committed = commit_body.index('"Phase" "committed"')
        cleanup = commit_body.index("Call CleanupCompletedInstallTransaction", committed)
        defer_without_rollback = commit_body.index(
            'StrCpy $InstallRecoveryFailed "0"', cleanup
        )
        self.assertLess(committed, cleanup)
        self.assertLess(cleanup, defer_without_rollback)

        rollback_start = SETUP_SOURCE.index("Function RollbackInstallTransaction")
        rollback_end = SETUP_SOURCE.index("FunctionEnd", rollback_start)
        rollback_body = SETUP_SOURCE[rollback_start:rollback_end]
        durable_phase = rollback_body.index(
            'ReadRegStr $InstallRecoveryPhase HKLM '
            '${INSTALLER_APP_RECOVERY_REGPATH} "Phase"'
        )
        committed_guard = rollback_body.index(
            '$InstallRecoveryPhase == "committed"', durable_phase
        )
        active_guard = rollback_body.index(
            '$InstallRecoveryPhase != "active"', committed_guard
        )
        active_snapshot = rollback_body.index(
            "Call ValidateActiveInstallRecoverySnapshot", active_guard
        )
        first_mutation = rollback_body.index(
            'ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} '
            '"UpdaterOperationStarted"',
            active_snapshot,
        )
        self.assertLess(durable_phase, committed_guard)
        self.assertLess(committed_guard, active_guard)
        self.assertLess(active_guard, active_snapshot)
        self.assertLess(active_snapshot, first_mutation)
        committed_slice = rollback_body[committed_guard:active_guard]
        self.assertIn("Call CleanupCompletedInstallTransaction", committed_slice)
        self.assertIn('StrCpy $InstallRollbackState "0"', committed_slice)
        self.assertNotIn("UpdaterOperationStarted", committed_slice)

    def test_rollback_requires_durable_active_phase_and_complete_snapshot(
        self,
    ) -> None:
        strict_start = SETUP_SOURCE.index(
            "Function ValidateActiveInstallRecoverySnapshot"
        )
        strict_end = SETUP_SOURCE.index("FunctionEnd", strict_start)
        strict_body = SETUP_SOURCE[strict_start:strict_end]
        for required_path in (
            "$InstallRecoveryProductRoot",
            "$InstallRollbackDirectory",
            "$InstallRollbackFiles",
        ):
            path = strict_body.index(
                f'StrCpy $InstallRecoveryPathToCheck "{required_path}"'
            )
            required = strict_body.index(
                'StrCpy $InstallRecoveryPathRequired "1"', path
            )
            validation = strict_body.index(
                "Call ValidateInstallRecoveryComponent", required
            )
            self.assertLess(path, required)
            self.assertLess(required, validation)
        self.assertIn(
            'GetFileAttributesW(w "$InstallRecoveryMarkerPath")', strict_body
        )
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", strict_body)
        self.assertIn("EqualizerAPO installer app-tree recovery v1", strict_body)

        rollback_start = SETUP_SOURCE.index("Function RollbackInstallTransaction")
        rollback_end = SETUP_SOURCE.index("FunctionEnd", rollback_start)
        rollback_body = SETUP_SOURCE[rollback_start:rollback_end]
        phase = rollback_body.index('"Phase"')
        committed = rollback_body.index('$InstallRecoveryPhase == "committed"', phase)
        active = rollback_body.index('$InstallRecoveryPhase != "active"', committed)
        snapshot = rollback_body.index(
            "Call ValidateActiveInstallRecoverySnapshot", active
        )
        task_mutation = rollback_body.index('"UpdaterOperationStarted"', snapshot)
        self.assertLess(phase, committed)
        self.assertLess(committed, active)
        self.assertLess(active, snapshot)
        self.assertLess(snapshot, task_mutation)
        self.assertNotIn('RMDir "$INSTDIR"', rollback_body)

    def test_uninstaller_avoids_unguarded_recursive_product_tree_deletion(
        self,
    ) -> None:
        recursive_commands = [
            line.strip()
            for line in SETUP_SOURCE.splitlines()
            if line.strip().startswith("RMDir ") and " /r " in line
        ]
        self.assertEqual(
            recursive_commands, ['RMDir /r "$InstallRollbackDirectory"']
        )

        validator_start = SETUP_SOURCE.index(
            "Function un.ValidateProductChildDirectory"
        )
        validator_end = SETUP_SOURCE.index("FunctionEnd", validator_start)
        validator_body = SETUP_SOURCE[validator_start:validator_end]
        self.assertGreaterEqual(validator_body.count("GetFullPathName"), 2)
        self.assertIn('StrCpy $2 "$0\\"', validator_body)
        self.assertIn("kernel32::GetFileAttributesW", validator_body)
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", validator_body)

        config_start = SETUP_SOURCE.index("Section /o un.$(SecRemoveName)")
        config_end = SETUP_SOURCE.index("SectionEnd", config_start)
        config_body = SETUP_SOURCE[config_start:config_end]
        self.assertIn("Call un.ValidateProductChildDirectory", config_body)
        self.assertIn('Delete "$InstallRecoveryPathToCheck\\*.*"', config_body)
        self.assertIn('RMDir /REBOOTOK "$InstallRecoveryPathToCheck"', config_body)
        self.assertNotIn("/r", config_body)

        qt_start = SETUP_SOURCE.index("Function un.RemoveQtPluginTreeSafely")
        qt_end = SETUP_SOURCE.index("FunctionEnd", qt_start)
        qt_body = SETUP_SOURCE[qt_start:qt_end]
        self.assertGreaterEqual(
            qt_body.count("Call un.ValidateProductChildDirectory"), 8
        )
        self.assertIn('Delete "$INSTDIR\\qt\\platforms\\qwindows.dll"', qt_body)
        self.assertIn('RMDir "$INSTDIR\\qt"', qt_body)
        self.assertNotIn("RMDir /r", qt_body)

    def test_post_registration_failures_are_checked_and_rolled_back(self) -> None:
        install_start = SETUP_SOURCE.index('Section "-Install"')
        install_end = SETUP_SOURCE.index("SectionEnd", install_start)
        install_body = SETUP_SOURCE[install_start:install_end]
        registration = install_body.index(
            'ExecWait \'"$SYSDIR\\regsvr32.exe" /s '
            '"$INSTDIR\\EqualizerAPO.dll"\' $1'
        )
        post_registration = install_body[registration:]
        self.assertIn("Pop $InstallOperationCode", post_registration)
        self.assertIn('${If} $InstallOperationCode == "error"', post_registration)
        self.assertIn('${ElseIf} $InstallOperationCode != 0', post_registration)
        self.assertIn('WriteUninstaller "$INSTDIR\\Uninstall.exe"', post_registration)
        self.assertIn('${If} ${Errors}', post_registration)
        self.assertIn(
            'ExecWait \'"$INSTDIR\\DeviceSelector.exe" /r /s\' '
            "$DeviceSelectorResult",
            post_registration,
        )
        self.assertIn(
            'ExecWait \'"$INSTDIR\\UpdateChecker.exe" -i -s\' '
            "$InstallOperationCode",
            post_registration,
        )
        self.assertIn(
            'WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} '
            '"UpdaterOperationStarted" 1',
            post_registration,
        )
        self.assertIn("installTransactionFailed:", post_registration)
        self.assertIn("Call RollbackInstallTransaction", post_registration)

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
        self.assertIn(
            '!insertmacro CreateRequiredShortcut '
            '"$SMPROGRAMS\\$StartMenuFolder\\Check for updates.lnk"',
            SETUP_SOURCE,
        )
        self.assertNotIn('RMDir /r "$SMPROGRAMS\\$OldStartMenuFolder"', SETUP_SOURCE)
        self.assertNotIn('RMDir /r "$SMPROGRAMS\\$StartMenuFolder"', SETUP_SOURCE)


if __name__ == "__main__":
    unittest.main()
