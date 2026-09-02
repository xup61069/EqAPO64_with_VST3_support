!include "LogicLib.nsh"
!include "MUI2.nsh"
!include "StrFunc.nsh"
!include "FileFunc.nsh"
!include "WinVer.nsh"
!include "x64.nsh"
!include "InstallerRecoveryManifest.nsh"

${StrTrimNewLines}
${StrTok}

Unicode true
ManifestDPIAware true
CRCCheck force
;Use more efficient compression
SetCompressor /SOLID lzma

!searchparse /file ..\version.h `#define MAJOR ` MAJOR
!searchparse /file ..\version.h `#define MINOR ` MINOR
!searchparse /file ..\version.h `#define REVISION ` REVISION
!define VERSION ${MAJOR}.${MINOR}.${REVISION}
!define PRODUCT_LABEL "Loudness Correction for Equalizer APO"
!define PRODUCT_FULL_LABEL "${PRODUCT_LABEL} (unofficial fork)"

!define REGPATH "Software\EqualizerAPO"
!define UNINST_REGPATH "Software\Microsoft\Windows\CurrentVersion\Uninstall\EqualizerAPO"
!define INSTALLER_RECOVERY_REGPATH "${REGPATH}\InstallerRecovery"
!define INSTALLER_APP_RECOVERY_REGPATH "${REGPATH}\InstallerAppRecovery"
!define CSIDL_COMMON_APPDATA 0x23
!define FILE_ATTRIBUTE_DIRECTORY 0x10
!define FILE_ATTRIBUTE_REPARSE_POINT 0x400
!define INVALID_FILE_ATTRIBUTES -1
!define ERROR_FILE_NOT_FOUND 2
!define ERROR_PATH_NOT_FOUND 3
!define INVALID_HANDLE_VALUE -1
!define FILE_READ_ATTRIBUTES 0x00000080
!define FILE_TYPE_DISK 1
!define FILE_SHARE_READ_WRITE 3
!define FILE_SHARE_READ_WRITE_DELETE 7
!define OPEN_EXISTING 3
!define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
!define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000
!define DELETE_ACCESS 0x00010000
!define FILE_DISPOSITION_INFO_CLASS 4
!define WIN32_HKEY_LOCAL_MACHINE 0x80000002
!define KEY_QUERY_VALUE 0x0001
!define KEY_WOW64_64KEY 0x0100
!define KEY_QUERY_VALUE_64 0x0101
!define INSTALL_RECOVERY_JOURNAL_VERSION 2

VIProductVersion "${MAJOR}.${MINOR}.${REVISION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "${PRODUCT_FULL_LABEL}"
VIAddVersionKey /LANG=1033 "FileDescription" "${PRODUCT_FULL_LABEL} ${TARGET_ARCH} Installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Equalizer APO contributors"

;--------------------------------
;General

  ;Name and file
  Name "${PRODUCT_FULL_LABEL} ${VERSION}"

  ;Request application privileges for Windows Vista
  RequestExecutionLevel admin

;--------------------------------
;Variables

  Var StartMenuFolder
  Var OldStartMenuFolder
  Var OLDINSTDIR
  Var ProtectedAudioValueExisted
  Var ProtectedAudioValue
  Var ProtectedAudioOverrideActive
  Var MissingAsset
  Var InstallRollbackDirectory
  Var InstallRecoveryCommonAppData
  Var InstallRecoveryProductRoot
  Var InstallRollbackFiles
  Var InstallRollbackState
  Var InstallRollbackCopyCode
  Var InstallRecoveryPhase
  Var InstallRecoveryFailed
  Var InstallRecoveryMarkerPath
  Var InstallRecoveryAclPath
  Var InstallRecoveryTaskXmlPath
  Var InstallRecoveryPathToCheck
  Var InstallRecoveryPathRequired
  Var InstallRecoveryPathExists
  Var InstallRecoveryAclTarget
  Var InstallRecoveryPhysicalPath
  Var InstallRecoveryPhysicalInstallPath
  Var InstallFailureReason
  Var InstallOperationCode
  Var PreviousApoPresent
  Var NewApoRegistrationAttempted
  Var RenameManifestPath
  Var RenameManifestHandle
  Var RenameManifestWriteFailed
  Var LegacyRenameRepairPath
  Var RenameIdentityPath
  Var RenameIdentityHandle
  Var RenameIdentityFailed
  Var RenameIdentityVolumeSerial
  Var RenameIdentityFileIndexHigh
  Var RenameIdentityFileIndexLow
  Var RenameExpectedVolumeSerial
  Var RenameExpectedFileIndexHigh
  Var RenameExpectedFileIndexLow
  Var ApoRollbackRegistrationCode
  Var ApoRollbackUnregistrationCode
  Var ApoRollbackStatus
  Var FailedRegistrationCode
  Var DeviceSelectorResult

;--------------------------------
;Interface Settings

  !define MUI_ABORTWARNING
  !define MUI_COMPONENTSPAGE_NODESC
  !define MUI_WELCOMEPAGE_TITLE_3LINES
  !define MUI_LANGDLL_REGISTRY_ROOT "HKLM"
  !define MUI_LANGDLL_REGISTRY_KEY ${REGPATH}
  !define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"

;--------------------------------
;Pages

  !insertmacro MUI_PAGE_WELCOME
  !insertmacro MUI_PAGE_LICENSE ..\LICENSE
  !insertmacro MUI_PAGE_DIRECTORY

;Start Menu Folder Page Configuration
  !define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
  !define MUI_STARTMENUPAGE_REGISTRY_KEY ${REGPATH}
  !define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"

  !insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
  !insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_INSTFILES
  !insertmacro MUI_PAGE_FINISH

  !insertmacro MUI_UNPAGE_WELCOME
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_COMPONENTS
  !insertmacro MUI_UNPAGE_INSTFILES
  !insertmacro MUI_UNPAGE_FINISH

;--------------------------------
;Languages

  !insertmacro MUI_LANGUAGE "English"
  !insertmacro MUI_LANGUAGE "Spanish"
  !insertmacro MUI_LANGUAGE "German"
  !insertmacro MUI_LANGUAGE "TradChinese"
  !insertmacro MUI_LANGUAGE "SimpChinese"

;--------------------------------
;Macros
Var renamePath
Var renameIndex
!macro RenameAndDelete path
  ${If} ${FileExists} "${path}"
    StrCpy $renamePath "${path}.old"
    StrCpy $renameIndex "0"
    ${While} ${FileExists} "$renamePath"
      StrCpy $renamePath "${path}.old.$renameIndex"
      IntOp $renameIndex $renameIndex + 1
    ${EndWhile}
    ; Capture the source file identity while holding a share-delete handle. The
    ; cleanup record is therefore tied to the file object, not merely its path.
    StrCpy $RenameIdentityPath "${path}"
    Call QueryRenameFileIdentityAndHold
    ${If} $RenameIdentityFailed == "1"
      DetailPrint "Could not identify an application file before renaming it: ${path}"
      Call RollbackInstallTransaction
      Abort
    ${EndIf}
    ClearErrors
    Rename "${path}" "$renamePath"
    ${If} ${Errors}
      Call CloseRenameIdentityHandle
      DetailPrint "Could not rename an in-use application file: ${path}"
      Call RollbackInstallTransaction
      Abort
    ${EndIf}
    ; Record only a Rename which the operating system confirmed. A crash before
    ; this append can leak a harmless .old file, but cleanup can never adopt a
    ; path which this transaction only planned to create.
    !insertmacro AppendInstallerRecoveryManifestLine \
      $RenameManifestHandle "$RenameManifestPath" \
      "C|$RenameIdentityVolumeSerial|$RenameIdentityFileIndexHigh|$RenameIdentityFileIndexLow|$renamePath|C" \
      $RenameManifestWriteFailed
    ${If} $RenameManifestWriteFailed == "1"
      Call CloseRenameIdentityHandle
      DetailPrint "Could not confirm a renamed application file: $renamePath"
      Call RollbackInstallTransaction
      Abort
    ${EndIf}
    ; Keep the original file object alive until its identity record has been
    ; closed, so its file ID cannot be recycled during the confirmation window.
    Call CloseRenameIdentityHandle
  ${EndIf}
!macroend

!macro RetireLegacyRenameRecoveryArtifact path
  System::Call 'kernel32::GetFileAttributesW(w "${path}") i .r0 ?e'
  Pop $1
  ${If} $0 != ${INVALID_FILE_ATTRIBUTES}
    IntOp $1 $0 & ${FILE_ATTRIBUTE_DIRECTORY}
    ${If} $1 != 0
      Goto retireLegacyRenameManifestFailed
    ${EndIf}
    IntOp $1 $0 & ${FILE_ATTRIBUTE_REPARSE_POINT}
    ${If} $1 != 0
      Goto retireLegacyRenameManifestFailed
    ${EndIf}
    ClearErrors
    Delete "${path}"
    ${If} ${Errors}
      Goto retireLegacyRenameManifestFailed
    ${EndIf}
  ${ElseIf} $1 != ${ERROR_FILE_NOT_FOUND}
  ${AndIf} $1 != ${ERROR_PATH_NOT_FOUND}
    Goto retireLegacyRenameManifestFailed
  ${EndIf}
!macroend

!macro DeleteProductShortcuts folder
  StrCpy $InstallOperationCode "0"
  ${If} "${folder}" != ""
    Delete "$SMPROGRAMS\${folder}\Equalizer APO Configuration Editor.lnk"
    ${If} ${FileExists} "$SMPROGRAMS\${folder}\Equalizer APO Configuration Editor.lnk"
      StrCpy $InstallOperationCode "1"
    ${EndIf}
    Delete "$SMPROGRAMS\${folder}\Configuration tutorial (online).lnk"
    ${If} ${FileExists} "$SMPROGRAMS\${folder}\Configuration tutorial (online).lnk"
      StrCpy $InstallOperationCode "1"
    ${EndIf}
    Delete "$SMPROGRAMS\${folder}\Configuration reference (online).lnk"
    ${If} ${FileExists} "$SMPROGRAMS\${folder}\Configuration reference (online).lnk"
      StrCpy $InstallOperationCode "1"
    ${EndIf}
    Delete "$SMPROGRAMS\${folder}\Equalizer APO Device Selector.lnk"
    ${If} ${FileExists} "$SMPROGRAMS\${folder}\Equalizer APO Device Selector.lnk"
      StrCpy $InstallOperationCode "1"
    ${EndIf}
    Delete "$SMPROGRAMS\${folder}\Benchmark.lnk"
    ${If} ${FileExists} "$SMPROGRAMS\${folder}\Benchmark.lnk"
      StrCpy $InstallOperationCode "1"
    ${EndIf}
    Delete "$SMPROGRAMS\${folder}\Check for updates.lnk"
    ${If} ${FileExists} "$SMPROGRAMS\${folder}\Check for updates.lnk"
      StrCpy $InstallOperationCode "1"
    ${EndIf}
    Delete "$SMPROGRAMS\${folder}\Uninstall.lnk"
    ${If} ${FileExists} "$SMPROGRAMS\${folder}\Uninstall.lnk"
      StrCpy $InstallOperationCode "1"
    ${EndIf}
    ; Keep unrelated user shortcuts and remove the folder only when empty.
    RMDir "$SMPROGRAMS\${folder}"
    ; A non-empty folder is expected when it contains unrelated shortcuts.
    ClearErrors
  ${EndIf}
!macroend

!macro RequireInstalledAsset path
  ${IfNot} ${FileExists} "${path}"
    StrCpy $MissingAsset "${path}"
    Goto missingRequiredAsset
  ${EndIf}
!macroend

; Remove only files owned by this installer. config and VSTPlugins are
; deliberately outside the transaction because they may contain user data.
!macro DeleteTransactionFile path
  Delete "${path}"
  ${If} ${FileExists} "${path}"
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
!macroend

; Persist previous registry values in the app-tree recovery journal. The
; journal lives in a peer key so protected-audio recovery can be completed and
; cleared independently.
!macro JournalPreviousString root key name journalName
  ClearErrors
  ReadRegStr $0 ${root} "${key}" "${name}"
  ${If} ${Errors}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Existed" 0
    ${If} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
    ${EndIf}
  ${Else}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Existed" 1
    ${If} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
    ${Else}
      ClearErrors
      WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Value" "$0"
      ${If} ${Errors}
        StrCpy $InstallRecoveryFailed "1"
      ${EndIf}
    ${EndIf}
  ${EndIf}
!macroend

!macro JournalPreviousDWORD root key name journalName
  ClearErrors
  ReadRegDWORD $0 ${root} "${key}" "${name}"
  ${If} ${Errors}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Existed" 0
    ${If} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
    ${EndIf}
  ${Else}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Existed" 1
    ${If} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
    ${Else}
      ClearErrors
      WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Value" $0
      ${If} ${Errors}
        StrCpy $InstallRecoveryFailed "1"
      ${EndIf}
    ${EndIf}
  ${EndIf}
!macroend

!macro RestorePreviousString root key name journalName
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Existed"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
  ${ElseIf} $0 == 1
    ClearErrors
    ReadRegStr $1 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Value"
    ${If} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
    ${Else}
      ClearErrors
      WriteRegStr ${root} "${key}" "${name}" "$1"
      ${If} ${Errors}
        StrCpy $InstallRecoveryFailed "1"
      ${Else}
        ClearErrors
        ReadRegStr $2 ${root} "${key}" "${name}"
        ${If} ${Errors}
          StrCpy $InstallRecoveryFailed "1"
        ${ElseIf} $2 != $1
          StrCpy $InstallRecoveryFailed "1"
        ${EndIf}
      ${EndIf}
    ${EndIf}
  ${ElseIf} $0 == 0
    DeleteRegValue ${root} "${key}" "${name}"
    ClearErrors
    ReadRegStr $1 ${root} "${key}" "${name}"
    ${IfNot} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
    ${EndIf}
  ${Else}
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
!macroend

!macro RestorePreviousDWORD root key name journalName
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Existed"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
  ${ElseIf} $0 == 1
    ClearErrors
    ReadRegDWORD $1 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "${journalName}Value"
    ${If} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
    ${Else}
      ClearErrors
      WriteRegDWORD ${root} "${key}" "${name}" $1
      ${If} ${Errors}
        StrCpy $InstallRecoveryFailed "1"
      ${Else}
        ClearErrors
        ReadRegDWORD $2 ${root} "${key}" "${name}"
        ${If} ${Errors}
          StrCpy $InstallRecoveryFailed "1"
        ${ElseIf} $2 != $1
          StrCpy $InstallRecoveryFailed "1"
        ${EndIf}
      ${EndIf}
    ${EndIf}
  ${ElseIf} $0 == 0
    DeleteRegValue ${root} "${key}" "${name}"
    ClearErrors
    ReadRegDWORD $1 ${root} "${key}" "${name}"
    ${IfNot} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
    ${EndIf}
  ${Else}
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
!macroend

!macro WriteRequiredRegStr root key name value description
  ClearErrors
  WriteRegStr ${root} "${key}" "${name}" "${value}"
  ${If} ${Errors}
    StrCpy $InstallFailureReason "${description}"
    Goto installTransactionFailed
  ${EndIf}
!macroend

!macro WriteRequiredRegDWORD root key name value description
  ClearErrors
  WriteRegDWORD ${root} "${key}" "${name}" ${value}
  ${If} ${Errors}
    StrCpy $InstallFailureReason "${description}"
    Goto installTransactionFailed
  ${EndIf}
!macroend

!macro CreateRequiredShortcut link target
  ClearErrors
  CreateShortCut "${link}" "${target}"
  ${If} ${Errors}
    StrCpy $InstallFailureReason "creating shortcut ${link}"
    Goto installTransactionFailed
  ${EndIf}
!macroend

LangString VersionError ${LANG_ENGLISH} "This installer is only supposed to be run on {0} Windows. Please use the {1} installer."
LangString VersionError ${LANG_SPANISH} "Este instalador solo debe ejecutarse en Windows {0}. Use el instalador {1}."
LangString VersionError ${LANG_GERMAN} "Dieses Installationsprogramm kann nur auf einem {0}-Windows verwendet werden. Bitte nutzen Sie die {1}-Version."
LangString VersionError ${LANG_TRADCHINESE} "此安裝程式僅能在 {0} Windows 上執行。請使用 {1} 安裝程式。"
LangString VersionError ${LANG_SIMPCHINESE} "此安装程序仅能在 {0} Windows 上运行。请使用 {1} 安装程序。"

LangString UCRTError ${LANG_ENGLISH} "Your Windows installation is missing required updates to use this program. Please install remaining Windows updates or the Visual C++ Redistributable for Visual Studio 2015 - 2022.$\n$\nDo you want to download the Visual C++ Redistributable now?"
LangString UCRTError ${LANG_SPANISH} "A su instalacion de Windows le faltan actualizaciones necesarias para usar este programa. Instale las actualizaciones pendientes de Windows o Visual C++ Redistributable para Visual Studio 2015 - 2022.$\n$\nDesea descargar Visual C++ Redistributable ahora?"
LangString UCRTError ${LANG_TRADCHINESE} "您的 Windows 系統缺少必要的更新以執行此程式。請安裝最新 Windows 更新或 Visual C++ 可轉散發套件。$\r$\n$\r$\n您要現在下載 Visual C++ 可轉散發套件嗎？"
LangString UCRTError ${LANG_SIMPCHINESE} "您的 Windows 系统缺少运行此程序所需的更新。请安装最新 Windows 更新或 Visual C++ 可再发行组件。$\r$\n$\r$\n您要现在下载 Visual C++ 可再发行组件吗？"
LangString UCRTError ${LANG_GERMAN} "Ihrer Windows-Installation fehlen benötigte Updates, um dieses Programm zu verwenden. Bitte installieren Sie ausstehende Windows-Updates oder das Visual C++ Redistributable für Visual Studio 2015 - 2022.$\n$\nMöchten Sie jetzt das Visual C++ Redistributable herunterladen?"
LangString CloseAppsPrompt ${LANG_ENGLISH} "Setup can close running Equalizer APO applications before installing. Unsaved configuration editor changes may be lost.$\n$\nDo you want setup to close them now?"
LangString CloseAppsPrompt ${LANG_SPANISH} "El instalador puede cerrar aplicaciones de Equalizer APO antes de instalar. Los cambios no guardados del editor de configuracion pueden perderse.$\n$\nDesea que el instalador las cierre ahora?"
LangString CloseAppsPrompt ${LANG_TRADCHINESE} "安裝程式可以在安裝前關閉正在執行的 Equalizer APO 應用程式。未儲存的設定檔編輯器變更可能會遺失。$\r$\n$\r$\n您要現在關閉它們嗎？"
LangString CloseAppsPrompt ${LANG_SIMPCHINESE} "安装程序可以在安装前关闭正在运行的 Equalizer APO 应用程序。未保存的配置编辑器更改可能会丢失。$\r$\n$\r$\n您要现在关闭它们吗？"
LangString CloseAppsPrompt ${LANG_GERMAN} "Das Setup kann laufende Equalizer APO-Anwendungen vor der Installation schließen. Nicht gespeicherte Änderungen im Konfigurationseditor können verloren gehen.$\n$\nSollen sie jetzt geschlossen werden?"
LangString RestorePointWarning ${LANG_ENGLISH} "Setup could not create a Windows restore point.$\n$\nThis can happen when System Protection is disabled, or when a restore point already exists from the last 24 hours. By default, Windows policy may block creating more than one restore point within the same 24-hour period.$\n$\nInstallation will continue."
LangString RestorePointWarning ${LANG_SPANISH} "El instalador no pudo crear un punto de restauracion de Windows.$\n$\nEsto puede ocurrir si Proteccion del sistema esta desactivada, o si ya existe un punto de restauracion creado en las ultimas 24 horas. De forma predeterminada, las politicas de Windows pueden bloquear la creacion de mas de un punto de restauracion dentro del mismo periodo de 24 horas.$\n$\nLa instalacion continuara."
LangString RestorePointWarning ${LANG_TRADCHINESE} "安裝程式無法建立 Windows 系統還原點。$\r$\n$\r$\n系統保護停用或過去 24 小時內已有還原點時，可能發生此情況。Windows 預設可能限制 24 小時內只能建立一個還原點。$\r$\n$\r$\n安裝將繼續。"
LangString RestorePointWarning ${LANG_SIMPCHINESE} "安装程序无法创建 Windows 系统还原点。$\r$\n$\r$\n系统保护被禁用或过去 24 小时内已有还原点时，可能发生此情况。Windows 默认可能限制 24 小时内只能创建一个还原点。$\r$\n$\r$\n安装将继续。"
LangString RestorePointWarning ${LANG_GERMAN} "Das Setup konnte keinen Windows-Wiederherstellungspunkt erstellen.$\n$\nDies kann passieren, wenn der Computerschutz deaktiviert ist oder wenn bereits ein Wiederherstellungspunkt aus den letzten 24 Stunden existiert. Standardmäßig kann Windows verhindern, dass innerhalb desselben 24-Stunden-Zeitraums mehr als ein Wiederherstellungspunkt erstellt wird.$\n$\nDie Installation wird fortgesetzt."

LangString AssetValidationError ${LANG_ENGLISH} "A required installation file is missing or could not be extracted. Setup will stop before registering Equalizer APO or modifying audio devices.$\r$\n$\r$\nMissing file: $MissingAsset"
LangString AssetValidationError ${LANG_SPANISH} "Falta un archivo de instalación necesario o no se pudo extraer. El instalador se detendrá antes de registrar Equalizer APO o modificar dispositivos de audio.$\r$\n$\r$\nArchivo faltante: $MissingAsset"
LangString AssetValidationError ${LANG_GERMAN} "Eine erforderliche Installationsdatei fehlt oder konnte nicht extrahiert werden. Das Setup wird beendet, bevor Equalizer APO registriert oder Audiogeräte geändert werden.$\r$\n$\r$\nFehlende Datei: $MissingAsset"
LangString AssetValidationError ${LANG_TRADCHINESE} "必要的安裝檔案遺失或無法解壓縮。安裝程式將在註冊 Equalizer APO 或修改音訊裝置前停止。$\r$\n$\r$\n遺失檔案：$MissingAsset"
LangString AssetValidationError ${LANG_SIMPCHINESE} "必要的安装文件缺失或无法解压缩。安装程序将在注册 Equalizer APO 或修改音频设备前停止。$\r$\n$\r$\n缺失文件：$MissingAsset"

;--------------------------------
;Functions
Function .onInit
  !if ${LIBPATH} != "lib32"
    SetRegView 64
  !endif
  ; Repair an interrupted prior registration attempt before reading any other
  ; installer state. The journal is written before the temporary audio override.
  Call RecoverProtectedAudioSetting
  ${If} $ProtectedAudioOverrideActive == "1"
    ${IfNot} ${Silent}
      MessageBox MB_ICONSTOP|MB_OK "Setup could not recover the protected-audio setting left by an interrupted installation. No installation files will be changed."
    ${EndIf}
    Abort
  ${EndIf}
  ; The application snapshot has its own durable journal and fixed recovery
  ; directory. Recover it before reading or overwriting normal installer state.
  Call RecoverInstallTransaction
  ${If} $InstallRecoveryFailed == "1"
    ${IfNot} ${Silent}
      MessageBox MB_ICONSTOP|MB_OK "Setup found an interrupted installation but could not recover it safely. The recovery journal and snapshot were retained; no new installation files will be changed."
    ${EndIf}
    Abort
  ${EndIf}
  !insertmacro MUI_LANGDLL_DISPLAY
  ;Get installation folder from registry if available
  ReadRegStr $INSTDIR HKLM ${REGPATH} "InstallPath"

  ;Use default installation folder otherwise
  ${If} $INSTDIR == ""
    StrCpy $INSTDIR "$PROGRAMFILES64\EqualizerAPO"
  ${EndIf}

  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
  ${If} ${IsNativeIA32}
    StrCpy $0 "x86"
  ${ElseIf} ${IsNativeAMD64}
    StrCpy $0 "x64"
  ${ElseIf} ${IsNativeARM64}
    StrCpy $0 "ARM64"
  ${EndIf}

  ${If} $0 != ${TARGET_ARCH}
    ${IfNot} ${Silent}
      MessageBox MB_OK|MB_ICONSTOP "This installer is only supposed to be run on ${TARGET_ARCH} Windows. Please use the $0 installer."
    ${EndIf}
    Abort
  ${EndIf}

  ${IfNot} ${AtLeastWin10}
    System::Call 'KERNEL32::LoadLibrary(t "ucrtbase.dll")p.r0'
    ${If} $0 P= 0
      ${IfNot} ${Silent}
        MessageBox MB_YESNO|MB_ICONSTOP $(UCRTError) IDNO skipDownload
        ExecShell "open" "${VCREDIST_URL}"
      ${EndIf}
      skipDownload:
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd

Function CloseRunningApplications
  ${If} ${FileExists} "$INSTDIR"
    ${IfNot} ${Silent}
      MessageBox MB_YESNO|MB_ICONQUESTION $(CloseAppsPrompt) IDNO done
    ${EndIf}
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM Editor.exe /T /F'
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM DeviceSelector.exe /T /F'
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM UpdateChecker.exe /T /F'
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM Benchmark.exe /T /F'
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM VoicemeeterClient.exe /T /F'
  ${EndIf}
  done:
FunctionEnd

Function CreateRestorePoint
  DetailPrint "Creating Windows restore point..."
  StrCpy $0 "$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
  ${IfNot} ${FileExists} "$0"
    StrCpy $0 "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe"
  ${EndIf}

  ${If} ${FileExists} "$0"
    nsExec::ExecToLog '"$0" -NoProfile -ExecutionPolicy Bypass -Command "try { Checkpoint-Computer -Description EqualizerAPO_${VERSION}_PreInstall -RestorePointType APPLICATION_INSTALL -ErrorAction Stop; exit 0 } catch { exit 1 }"'
    Pop $1
    ${If} $1 == 0
      DetailPrint "Windows restore point created."
    ${Else}
      DetailPrint "Windows restore point was not created. PowerShell exit code: $1"
      ${IfNot} ${Silent}
        MessageBox MB_ICONEXCLAMATION|MB_OK $(RestorePointWarning)
      ${EndIf}
    ${EndIf}
  ${Else}
    DetailPrint "PowerShell was not found. Skipping restore point creation."
    ${IfNot} ${Silent}
      MessageBox MB_ICONEXCLAMATION|MB_OK $(RestorePointWarning)
    ${EndIf}
  ${EndIf}
FunctionEnd

Function SaveProtectedAudioSetting
  ClearErrors
  ReadRegDWORD $ProtectedAudioValue HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG"
  ${If} ${Errors}
    StrCpy $ProtectedAudioValueExisted "0"
    StrCpy $ProtectedAudioValue "0"
  ${Else}
    StrCpy $ProtectedAudioValueExisted "1"
  ${EndIf}
FunctionEnd

Function BeginProtectedAudioOverride
  ; Never overwrite a still-pending journal from an earlier attempt in this
  ; process. If it cannot be restored, the caller must not run regsvr32.
  ${If} $ProtectedAudioOverrideActive == "1"
    Call RestoreProtectedAudioSetting
    ${If} $ProtectedAudioOverrideActive == "1"
      Return
    ${EndIf}
  ${EndIf}

  Call SaveProtectedAudioSetting
  ; Journal value data first and mark it pending last. The protected-audio value
  ; is changed only after the durable pending marker exists.
  ClearErrors
  WriteRegDWORD HKLM ${INSTALLER_RECOVERY_REGPATH} "ValueExisted" $ProtectedAudioValueExisted
  ${If} ${Errors}
    Goto protectedAudioJournalFailed
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_RECOVERY_REGPATH} "Value" $ProtectedAudioValue
  ${If} ${Errors}
    Goto protectedAudioJournalFailed
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_RECOVERY_REGPATH} "Pending" 1
  ${If} ${Errors}
    Goto protectedAudioJournalFailed
  ${EndIf}
  StrCpy $ProtectedAudioOverrideActive "1"
  ClearErrors
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG" 1
  ${If} ${Errors}
    Call RestoreProtectedAudioSetting
  ${EndIf}
  Return

  protectedAudioJournalFailed:
  DeleteRegKey HKLM ${INSTALLER_RECOVERY_REGPATH}
  DeleteRegKey /ifempty HKLM ${REGPATH}
  StrCpy $ProtectedAudioOverrideActive "0"
  DetailPrint "Could not create the protected-audio recovery journal."
FunctionEnd

Function RestoreProtectedAudioSetting
  StrCpy $ProtectedAudioOverrideActive "1"
  ${If} $ProtectedAudioValueExisted == "1"
    ClearErrors
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG" $ProtectedAudioValue
    ${If} ${Errors}
      DetailPrint "Could not restore the protected-audio registry value; the recovery journal was retained."
      Return
    ${EndIf}
    ClearErrors
    ReadRegDWORD $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG"
    ${If} ${Errors}
      DetailPrint "Could not verify the restored protected-audio registry value; the recovery journal was retained."
      Return
    ${ElseIf} $0 != $ProtectedAudioValue
      DetailPrint "The protected-audio registry value did not verify; the recovery journal was retained."
      Return
    ${EndIf}
  ${Else}
    DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG"
    ; Verify absence rather than trusting DeleteRegValue's error flag; deleting
    ; a value which is already absent is also a successful restoration.
    ClearErrors
    ReadRegDWORD $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG"
    ${IfNot} ${Errors}
      DetailPrint "Could not remove the temporary protected-audio registry value; the recovery journal was retained."
      Return
    ${EndIf}
  ${EndIf}

  DeleteRegKey HKLM ${INSTALLER_RECOVERY_REGPATH}
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_RECOVERY_REGPATH} "Pending"
  ${IfNot} ${Errors}
    DetailPrint "Could not clear the protected-audio recovery journal."
    Return
  ${EndIf}
  DeleteRegKey /ifempty HKLM ${REGPATH}
  StrCpy $ProtectedAudioOverrideActive "0"
FunctionEnd

Function RecoverProtectedAudioSetting
  StrCpy $ProtectedAudioOverrideActive "0"
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_RECOVERY_REGPATH} "Pending"
  ${If} ${Errors}
    Return
  ${EndIf}
  ${If} $0 != 1
    ; A present but malformed pending marker must not be overwritten because its
    ; intended recovery state is unknown.
    StrCpy $ProtectedAudioOverrideActive "1"
    Return
  ${EndIf}

  ClearErrors
  ReadRegDWORD $1 HKLM ${INSTALLER_RECOVERY_REGPATH} "ValueExisted"
  ${If} ${Errors}
    StrCpy $ProtectedAudioOverrideActive "1"
    Return
  ${EndIf}
  ${If} $1 != 0
  ${AndIf} $1 != 1
    StrCpy $ProtectedAudioOverrideActive "1"
    Return
  ${EndIf}
  ReadRegDWORD $2 HKLM ${INSTALLER_RECOVERY_REGPATH} "Value"
  ${If} ${Errors}
    StrCpy $ProtectedAudioOverrideActive "1"
    Return
  ${EndIf}
  StrCpy $ProtectedAudioValueExisted "$1"
  StrCpy $ProtectedAudioValue "$2"
  StrCpy $ProtectedAudioOverrideActive "1"
  Call RestoreProtectedAudioSetting
  ${If} $ProtectedAudioOverrideActive == "0"
    DetailPrint "Recovered the protected-audio setting from an interrupted installation."
  ${EndIf}
FunctionEnd

Function InitializeInstallRecoveryPaths
  ; Never load these paths from the journal. Keeping the recovery root fixed
  ; prevents a stale or malformed registry value from selecting a broad tree for
  ; recursive cleanup.
  StrCpy $InstallRecoveryFailed "0"
  StrCpy $InstallRecoveryCommonAppData ""
  StrCpy $InstallRecoveryProductRoot ""
  StrCpy $InstallRollbackDirectory ""
  System::Call 'shell32::SHGetFolderPathW(p 0, i ${CSIDL_COMMON_APPDATA}, p 0, i 0, w .r0) i .r1'
  ${If} $1 != 0
  ${OrIf} $0 == ""
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ClearErrors
  GetFullPathName $2 "$0"
  ${If} ${Errors}
  ${OrIf} $2 == ""
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryCommonAppData "$2"
  StrCpy $InstallRecoveryProductRoot "$InstallRecoveryCommonAppData\EqualizerAPO"
  StrCpy $InstallRollbackDirectory "$InstallRecoveryProductRoot\InstallerRecovery"
  StrCpy $InstallRollbackFiles "$InstallRollbackDirectory\files"
  StrCpy $RenameManifestPath "$InstallRollbackDirectory\renamed-files.txt"
  StrCpy $InstallRecoveryMarkerPath "$InstallRollbackDirectory\app-tree.marker"
  StrCpy $InstallRecoveryAclPath "$InstallRollbackDirectory\config-acl.txt"
  StrCpy $InstallRecoveryTaskXmlPath "$InstallRollbackDirectory\update-task.xml"
FunctionEnd

Function ValidateInstallRecoveryComponent
  ; Inputs are carried in dedicated variables so callers cannot accidentally
  ; trust a path loaded from the recovery journal. Missing optional descendants
  ; are allowed; access errors and every existing reparse point fail closed.
  StrCpy $InstallRecoveryPathExists "0"
  System::Call 'kernel32::GetFileAttributesW(w "$InstallRecoveryPathToCheck") i .r0 ?e'
  Pop $1
  ${If} $0 == ${INVALID_FILE_ATTRIBUTES}
    ${If} $InstallRecoveryPathRequired == "0"
      ${If} $1 == ${ERROR_FILE_NOT_FOUND}
      ${OrIf} $1 == ${ERROR_PATH_NOT_FOUND}
        Return
      ${EndIf}
    ${EndIf}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  IntOp $1 $0 & ${FILE_ATTRIBUTE_DIRECTORY}
  ${If} $1 == 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  IntOp $1 $0 & ${FILE_ATTRIBUTE_REPARSE_POINT}
  ${If} $1 != 0
    DetailPrint "Refusing a recovery path containing a reparse point: $InstallRecoveryPathToCheck"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryPathExists "1"
FunctionEnd

Function ValidateInstallRecoveryComponents
  StrCpy $InstallRecoveryFailed "0"
  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryCommonAppData"
  StrCpy $InstallRecoveryPathRequired "1"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackDirectory"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackFiles"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
FunctionEnd

Function HardenInstallRecoveryDirectory
  ; Apply a protected DACL in one Win32 operation. The SDDL grants full control
  ; only to SYSTEM and built-in Administrators and avoids an icacls reset window
  ; in which inherited Users permissions could briefly return.
  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryAclTarget"
  StrCpy $InstallRecoveryPathRequired "1"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  StrCpy $0 0
  System::Call 'advapi32::ConvertStringSecurityDescriptorToSecurityDescriptorW(w "O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", i 1, *p .r0, p 0) i .r1'
  ${If} $1 == 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  System::Call 'advapi32::SetFileSecurityW(w "$InstallRecoveryAclTarget", i 0x80000007, p r0) i .r2 ?e'
  Pop $3
  System::Call 'kernel32::LocalFree(p r0) p .r4'
  ${If} $2 == 0
    DetailPrint "Could not secure recovery ACL for $InstallRecoveryAclTarget (Win32 error $3)."
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryAclTarget"
  StrCpy $InstallRecoveryPathRequired "1"
  Call ValidateInstallRecoveryComponent
FunctionEnd

Function CreateInstallRecoveryDirectoryAtomically
  ; Official NSIS stubs are 32-bit, so SECURITY_ATTRIBUTES is 12 bytes. Pass a
  ; protected SDDL at CreateDirectoryW time: no inherited writable-ACL window is
  ; exposed between directory creation and hardening.
  StrCpy $0 0
  System::Call 'advapi32::ConvertStringSecurityDescriptorToSecurityDescriptorW(w "O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", i 1, *p .r0, p 0) i .r1'
  ${If} $1 == 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  System::Call '*(i 12, p r0, i 0) p .r2'
  ${If} $2 == 0
    System::Call 'kernel32::LocalFree(p r0) p .r5'
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  System::Call 'kernel32::CreateDirectoryW(w "$InstallRecoveryAclTarget", p r2) i .r3 ?e'
  Pop $4
  System::Free $2
  System::Call 'kernel32::LocalFree(p r0) p .r5'
  ${If} $3 == 0
    DetailPrint "Could not atomically create secure recovery directory $InstallRecoveryAclTarget (Win32 error $4)."
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryAclTarget"
  StrCpy $InstallRecoveryPathRequired "1"
  Call ValidateInstallRecoveryComponent
FunctionEnd

Function SecureExistingInstallRecoveryTree
  Call ValidateInstallRecoveryComponents
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ProductRootCreated"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ; Pending is persisted before filesystem creation. If power is lost in that
  ; interval, ProductRootCreated=0 and no tree is a clean, recoverable state.
  ; Conversely, a tree with no ownership marker is never adopted or deleted.
  ${If} $0 == 0
    StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
    StrCpy $InstallRecoveryPathRequired "0"
    Call ValidateInstallRecoveryComponent
    ${If} $InstallRecoveryFailed == "1"
      Return
    ${ElseIf} $InstallRecoveryPathExists == "0"
      Return
    ${EndIf}
    DetailPrint "A recovery product root exists without a verified ownership marker and was retained: $InstallRecoveryProductRoot"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${ElseIf} $0 != 1
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  ${If} $InstallRecoveryPathExists == "0"
    Return
  ${EndIf}

  ; A missing recovery child means recursive cleanup already finished. Its
  ; secured parent is handled non-recursively by the journaled cleanup caller.
  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackDirectory"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
  ${OrIf} $InstallRecoveryPathExists == "0"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryAclTarget "$InstallRecoveryProductRoot"
  Call HardenInstallRecoveryDirectory
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryAclTarget "$InstallRollbackDirectory"
  Call HardenInstallRecoveryDirectory
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackFiles"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  ${If} $InstallRecoveryPathExists == "1"
    StrCpy $InstallRecoveryAclTarget "$InstallRollbackFiles"
    Call HardenInstallRecoveryDirectory
    ${If} $InstallRecoveryFailed == "1"
      Return
    ${EndIf}
  ${EndIf}
  Call ValidateInstallRecoveryComponents
FunctionEnd

Function ValidateActiveInstallRecoverySnapshot
  ; Destructive rollback is allowed only with a complete, authenticated active
  ; snapshot. Cleanup phases deliberately use the optional validator instead.
  Call SecureExistingInstallRecoveryTree
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ProductRootCreated"
  ${If} ${Errors}
  ${OrIf} $0 != 1
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryCommonAppData"
  StrCpy $InstallRecoveryPathRequired "1"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
  StrCpy $InstallRecoveryPathRequired "1"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackDirectory"
  StrCpy $InstallRecoveryPathRequired "1"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackFiles"
  StrCpy $InstallRecoveryPathRequired "1"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  System::Call 'kernel32::GetFileAttributesW(w "$InstallRecoveryMarkerPath") i .r0 ?e'
  Pop $1
  ${If} $0 == ${INVALID_FILE_ATTRIBUTES}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  IntOp $1 $0 & ${FILE_ATTRIBUTE_DIRECTORY}
  ${If} $1 != 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  IntOp $1 $0 & ${FILE_ATTRIBUTE_REPARSE_POINT}
  ${If} $1 != 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ClearErrors
  FileOpen $0 "$InstallRecoveryMarkerPath" r
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ClearErrors
  FileRead $0 $1
  ${If} ${Errors}
    FileClose $0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  FileClose $0
  ${StrTrimNewLines} $1 "$1"
  ${If} $1 != "EqualizerAPO installer app-tree recovery v1"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ; Revalidate the protected directory chain immediately before the caller uses
  ; it. Its SYSTEM/Administrators-only ACL prevents a lower-privilege swap.
  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackFiles"
  StrCpy $InstallRecoveryPathRequired "1"
  Call ValidateInstallRecoveryComponent
FunctionEnd

Function CreateSecureInstallRecoveryTree
  ; The journal is already durable before this function is entered. An existing
  ; recovery directory is never adopted as ours.
  Call ValidateInstallRecoveryComponents
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackDirectory"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
  ${OrIf} $InstallRecoveryPathExists == "1"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ; The parent product root is also a security boundary. Never adopt a directory
  ; which could have been pre-created by an unprivileged user.
  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
  ${OrIf} $InstallRecoveryPathExists == "1"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryAclTarget "$InstallRecoveryProductRoot"
  Call CreateInstallRecoveryDirectoryAtomically
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  ClearErrors
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ProductRootCreated" 1
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ProductRootCreated"
  ${If} ${Errors}
  ${OrIf} $0 != 1
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryAclTarget "$InstallRollbackDirectory"
  Call CreateInstallRecoveryDirectoryAtomically
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryAclTarget "$InstallRollbackFiles"
  Call CreateInstallRecoveryDirectoryAtomically
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  Call ValidateInstallRecoveryComponents
FunctionEnd

Function ResolveInstallRecoveryPhysicalPath
  ; Resolve every existing ancestor reparse point through a directory handle.
  ; GetFullPathName is only lexical and is not sufficient for the install versus
  ; recovery containment decision.
  StrCpy $InstallRecoveryPhysicalPath ""
  ClearErrors
  System::Call 'kernel32::CreateFileW(w "$InstallRecoveryPathToCheck", i 0, i ${FILE_SHARE_READ_WRITE_DELETE}, p 0, i ${OPEN_EXISTING}, i ${FILE_FLAG_BACKUP_SEMANTICS}, p 0) p .r0 ?e'
  Pop $1
  ${If} $0 == ${INVALID_HANDLE_VALUE}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  System::Alloc 4096
  Pop $2
  ${If} $2 == 0
    System::Call 'kernel32::CloseHandle(p r0) i .r3'
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  System::Call 'kernel32::GetFinalPathNameByHandleW(p r0, p r2, i 2048, i 0) i .r3 ?e'
  Pop $1
  System::Call 'kernel32::CloseHandle(p r0) i .r5'
  ${If} $3 == 0
  ${OrIf} $3 >= 1024
    System::Free $2
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  System::Call '*$2(&w1024 .r4)'
  System::Free $2
  ${If} $4 == ""
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ; Normalize the Win32 extended prefixes before delimiter-aware comparison.
  StrCpy $5 "$4" 8
  ${If} $5 == "\\?\UNC\"
    StrCpy $6 "$4" "" 8
    StrCpy $4 "\\$6"
  ${Else}
    StrCpy $5 "$4" 4
    ${If} $5 == "\\?\"
      StrCpy $4 "$4" "" 4
    ${EndIf}
  ${EndIf}
  normalizePhysicalPathTail:
  StrCpy $5 "$4" 1 -1
  ${If} $5 == "\"
    ${GetRoot} "$4" $6
    ${If} $4 != $6
    ${AndIf} $4 != "$6\"
      StrCpy $4 "$4" -1
      Goto normalizePhysicalPathTail
    ${EndIf}
  ${EndIf}
  StrCpy $InstallRecoveryPhysicalPath "$4"
FunctionEnd

Function ValidateInstallRecoveryTarget
  StrCpy $InstallRecoveryFailed "0"
  ${If} $INSTDIR == ""
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ClearErrors
  GetFullPathName $0 "$INSTDIR"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  normalizeInstallPathTail:
  StrCpy $6 "$0" 1 -1
  ${If} $6 == "\"
    ${GetRoot} "$0" $7
    ${If} $0 != $7
    ${AndIf} $0 != "$7\"
      StrCpy $0 "$0" -1
      Goto normalizeInstallPathTail
    ${EndIf}
  ${EndIf}
  StrCpy $INSTDIR "$0"
  ; GetFullPathName fails when the final directory does not exist, which is the
  ; normal state before the first recovery snapshot is created. Canonicalize the
  ; existing trusted Common AppData root, then append only fixed product-owned
  ; components. None of these descendants are loaded from installer input or the
  ; recovery journal.
  ClearErrors
  GetFullPathName $2 "$InstallRecoveryCommonAppData"
  ${If} ${Errors}
  ${OrIf} $2 == ""
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  normalizeCommonAppDataTail:
  StrCpy $6 "$2" 1 -1
  ${If} $6 == "\"
    ${GetRoot} "$2" $7
    ${If} $2 != $7
    ${AndIf} $2 != "$7\"
      StrCpy $2 "$2" -1
      Goto normalizeCommonAppDataTail
    ${EndIf}
  ${EndIf}
  StrCpy $InstallRecoveryCommonAppData "$2"
  StrCpy $InstallRecoveryProductRoot "$InstallRecoveryCommonAppData\EqualizerAPO"
  StrCpy $InstallRollbackDirectory "$InstallRecoveryProductRoot\InstallerRecovery"
  StrCpy $InstallRollbackFiles "$InstallRollbackDirectory\files"
  StrCpy $RenameManifestPath "$InstallRollbackDirectory\renamed-files.txt"
  StrCpy $InstallRecoveryMarkerPath "$InstallRollbackDirectory\app-tree.marker"
  StrCpy $InstallRecoveryAclPath "$InstallRollbackDirectory\config-acl.txt"
  StrCpy $InstallRecoveryTaskXmlPath "$InstallRollbackDirectory\update-task.xml"
  StrLen $1 "$INSTDIR"
  ${If} $1 < 4
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ${GetRoot} "$INSTDIR" $0
  ${If} $INSTDIR == $0
  ${OrIf} $INSTDIR == "$0\"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ${If} $INSTDIR == "$WINDIR"
  ${OrIf} $INSTDIR == "$SYSDIR"
  ${OrIf} $INSTDIR == "$PROGRAMFILES"
  ${OrIf} $INSTDIR == "$PROGRAMFILES32"
  ${OrIf} $INSTDIR == "$PROGRAMFILES64"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ; Compare canonical paths with a delimiter on both sides. This rejects exact,
  ; ancestor, and descendant relationships without treating InstallerRecovery2
  ; as a child of InstallerRecovery.
  StrCpy $2 "$INSTDIR\"
  StrCpy $3 "$InstallRollbackDirectory\"
  StrLen $4 "$2"
  StrCpy $5 "$3" $4
  ${If} $5 == $2
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  StrLen $4 "$3"
  StrCpy $5 "$2" $4
  ${If} $5 == $3
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ; Lexical normalization cannot see an ancestor junction. Resolve the existing
  ; install directory and Common AppData through handles, then repeat the same
  ; exact/ancestor/descendant comparison against their physical paths.
  StrCpy $InstallRecoveryPathToCheck "$INSTDIR"
  Call ResolveInstallRecoveryPhysicalPath
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryPhysicalInstallPath "$InstallRecoveryPhysicalPath"
  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryCommonAppData"
  Call ResolveInstallRecoveryPhysicalPath
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  StrCpy $2 "$InstallRecoveryPhysicalInstallPath\"
  StrCpy $3 "$InstallRecoveryPhysicalPath\EqualizerAPO\InstallerRecovery\"
  StrLen $4 "$2"
  StrCpy $5 "$3" $4
  ${If} $5 == $2
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  StrLen $4 "$3"
  StrCpy $5 "$2" $4
  ${If} $5 == $3
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ; Once a transaction is journaled, the physical target must remain stable.
  ; A junction retarget between installer runs is rejected before any cleanup or
  ; rollback file operation.
  ClearErrors
  ReadRegStr $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PhysicalInstallPath"
  ${IfNot} ${Errors}
  ${AndIf} $0 != "$InstallRecoveryPhysicalInstallPath"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
FunctionEnd

Function SaveInstallMetadataJournal
  StrCpy $InstallRecoveryFailed "0"
  !insertmacro JournalPreviousString HKLM "${REGPATH}" "InstallPath" "PreviousInstallPath"
  !insertmacro JournalPreviousString HKLM "${REGPATH}" "ConfigPath" "PreviousConfigPath"
  !insertmacro JournalPreviousString HKLM "${REGPATH}" "EnableTrace" "PreviousEnableTrace"
  !insertmacro JournalPreviousString HKLM "${REGPATH}" "Start Menu Folder" "PreviousStartMenuFolder"
  !insertmacro JournalPreviousString HKLM "${UNINST_REGPATH}" "DisplayName" "PreviousDisplayName"
  !insertmacro JournalPreviousString HKLM "${UNINST_REGPATH}" "DisplayVersion" "PreviousDisplayVersion"
  !insertmacro JournalPreviousString HKLM "${UNINST_REGPATH}" "UninstallString" "PreviousUninstallString"
  !insertmacro JournalPreviousDWORD HKLM "${UNINST_REGPATH}" "NoModify" "PreviousNoModify"
  !insertmacro JournalPreviousDWORD HKLM "${UNINST_REGPATH}" "NoRepair" "PreviousNoRepair"
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  ClearErrors
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "InstallPath" "$INSTDIR"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ${If} $InstallRecoveryPhysicalInstallPath == ""
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PhysicalInstallPath" "$InstallRecoveryPhysicalInstallPath"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  Call PersistInstallRecoveryJournalVersion
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  ClearErrors
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "RenameCleanupStarted" 0
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "OldStartMenuFolder" "$OldStartMenuFolder"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "NewStartMenuFolder" "$StartMenuFolder"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousApoPresent" $PreviousApoPresent
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "NewApoRegistrationAttempted" 0
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "UpdaterOperationStarted" 0
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousUpdateTaskXmlSaved" 0
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ProductRootCreated" 0
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "DeviceSelectorOperationStarted" 0
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "DeviceSelectorMode" 0
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
FunctionEnd

Function RestoreInstallMetadata
  StrCpy $InstallRecoveryFailed "0"
  !insertmacro RestorePreviousString HKLM "${REGPATH}" "InstallPath" "PreviousInstallPath"
  !insertmacro RestorePreviousString HKLM "${REGPATH}" "ConfigPath" "PreviousConfigPath"
  !insertmacro RestorePreviousString HKLM "${REGPATH}" "EnableTrace" "PreviousEnableTrace"
  !insertmacro RestorePreviousString HKLM "${REGPATH}" "Start Menu Folder" "PreviousStartMenuFolder"
  !insertmacro RestorePreviousString HKLM "${UNINST_REGPATH}" "DisplayName" "PreviousDisplayName"
  !insertmacro RestorePreviousString HKLM "${UNINST_REGPATH}" "DisplayVersion" "PreviousDisplayVersion"
  !insertmacro RestorePreviousString HKLM "${UNINST_REGPATH}" "UninstallString" "PreviousUninstallString"
  !insertmacro RestorePreviousDWORD HKLM "${UNINST_REGPATH}" "NoModify" "PreviousNoModify"
  !insertmacro RestorePreviousDWORD HKLM "${UNINST_REGPATH}" "NoRepair" "PreviousNoRepair"
  DeleteRegKey /ifempty HKLM ${UNINST_REGPATH}
FunctionEnd

Function RestoreInstallShortcuts
  ClearErrors
  ReadRegStr $StartMenuFolder HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "NewStartMenuFolder"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ReadRegStr $OldStartMenuFolder HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "OldStartMenuFolder"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ClearErrors
  !insertmacro DeleteProductShortcuts $StartMenuFolder
  ${If} $InstallOperationCode != 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousInstallPathExisted"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ${If} $0 == 1
  ${AndIf} $OldStartMenuFolder != ""
    ReadRegStr $OLDINSTDIR HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousInstallPathValue"
    ${If} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
      Return
    ${EndIf}
    ClearErrors
    CreateDirectory "$SMPROGRAMS\$OldStartMenuFolder"
    CreateShortCut "$SMPROGRAMS\$OldStartMenuFolder\Equalizer APO Configuration Editor.lnk" "$OLDINSTDIR\Editor.exe"
    CreateShortCut "$SMPROGRAMS\$OldStartMenuFolder\Configuration tutorial (online).lnk" "$OLDINSTDIR\Configuration tutorial (online).url"
    CreateShortCut "$SMPROGRAMS\$OldStartMenuFolder\Configuration reference (online).lnk" "$OLDINSTDIR\Configuration reference (online).url"
    CreateShortCut "$SMPROGRAMS\$OldStartMenuFolder\Equalizer APO Device Selector.lnk" "$OLDINSTDIR\DeviceSelector.exe"
    CreateShortCut "$SMPROGRAMS\$OldStartMenuFolder\Benchmark.lnk" "$OLDINSTDIR\Benchmark.exe"
    CreateShortCut "$SMPROGRAMS\$OldStartMenuFolder\Check for updates.lnk" "$OLDINSTDIR\UpdateChecker.exe"
    CreateShortCut "$SMPROGRAMS\$OldStartMenuFolder\Uninstall.lnk" "$OLDINSTDIR\Uninstall.exe"
    ${If} ${Errors}
      StrCpy $InstallRecoveryFailed "1"
      Return
    ${EndIf}
  ${ElseIf} $0 != 0
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
FunctionEnd

Function CloseRenameIdentityHandle
  ${If} $RenameIdentityHandle != ""
  ${AndIf} $RenameIdentityHandle != ${INVALID_HANDLE_VALUE}
    System::Call 'kernel32::CloseHandle(p $RenameIdentityHandle) i .r0'
  ${EndIf}
  StrCpy $RenameIdentityHandle ""
FunctionEnd

Function QueryRenameFileIdentityAndHold
  Call CloseRenameIdentityHandle
  StrCpy $RenameIdentityFailed "1"
  StrCpy $1 0
  System::Call 'kernel32::CreateFileW(w "$RenameIdentityPath", i ${FILE_READ_ATTRIBUTES}, i ${FILE_SHARE_READ_WRITE_DELETE}, p 0, i ${OPEN_EXISTING}, i ${FILE_FLAG_OPEN_REPARSE_POINT}|${FILE_FLAG_BACKUP_SEMANTICS}, p 0) p .r0 ?e'
  Pop $2
  ${If} $0 == ${INVALID_HANDLE_VALUE}
    Return
  ${EndIf}
  StrCpy $RenameIdentityHandle "$0"

  System::Call 'kernel32::GetFileType(p r0) i .r6'
  ${If} $6 != ${FILE_TYPE_DISK}
    Goto queryRenameIdentityFailed
  ${EndIf}
  System::Call '*(i,&v24,i,&v12,i,i) p .r1'
  ${If} $1 == 0
    Goto queryRenameIdentityFailed
  ${EndIf}
  System::Call 'kernel32::GetFileInformationByHandle(p r0, p r1) i .r6 ?e'
  Pop $7
  ${If} $6 == 0
    Goto queryRenameIdentityFailed
  ${EndIf}
  ; BY_HANDLE_FILE_INFORMATION: attributes, six FILETIME DWORDs, volume,
  ; size high/low, link count, then file-index high/low.
  System::Call '*$1(i .r2, &v24, i .r3, &v12, i .r4, i .r5)'
  IntOp $6 $2 & ${FILE_ATTRIBUTE_DIRECTORY}
  ${If} $6 != 0
    Goto queryRenameIdentityFailed
  ${EndIf}
  IntOp $6 $2 & ${FILE_ATTRIBUTE_REPARSE_POINT}
  ${If} $6 != 0
    Goto queryRenameIdentityFailed
  ${EndIf}
  ${If} $4 == 0
  ${AndIf} $5 == 0
    Goto queryRenameIdentityFailed
  ${EndIf}

  StrCpy $RenameIdentityVolumeSerial "$3"
  StrCpy $RenameIdentityFileIndexHigh "$4"
  StrCpy $RenameIdentityFileIndexLow "$5"
  System::Free $1
  StrCpy $RenameIdentityFailed "0"
  Return

  queryRenameIdentityFailed:
  ${If} $1 != 0
    System::Free $1
  ${EndIf}
  Call CloseRenameIdentityHandle
FunctionEnd

Function DeleteRenameFileByIdentity
  ; Open the candidate without following a reparse point, compare its stable file
  ; identity, and mark that same handle for deletion. No path-based TOCTOU window
  ; remains between the identity check and deletion.
  Call CloseRenameIdentityHandle
  StrCpy $RenameIdentityFailed "1"
  StrCpy $1 0
  StrCpy $6 0
  System::Call 'kernel32::CreateFileW(w "$RenameIdentityPath", i ${DELETE_ACCESS}|${FILE_READ_ATTRIBUTES}, i ${FILE_SHARE_READ_WRITE}, p 0, i ${OPEN_EXISTING}, i ${FILE_FLAG_OPEN_REPARSE_POINT}|${FILE_FLAG_BACKUP_SEMANTICS}, p 0) p .r0 ?e'
  Pop $2
  ${If} $0 == ${INVALID_HANDLE_VALUE}
    ${If} $2 == ${ERROR_FILE_NOT_FOUND}
    ${OrIf} $2 == ${ERROR_PATH_NOT_FOUND}
      StrCpy $RenameIdentityFailed "0"
    ${EndIf}
    Return
  ${EndIf}
  StrCpy $RenameIdentityHandle "$0"

  System::Call 'kernel32::GetFileType(p r0) i .r7'
  ${If} $7 != ${FILE_TYPE_DISK}
    Goto deleteRenameIdentityFailed
  ${EndIf}
  System::Call '*(i,&v24,i,&v12,i,i) p .r1'
  ${If} $1 == 0
    Goto deleteRenameIdentityFailed
  ${EndIf}
  System::Call 'kernel32::GetFileInformationByHandle(p r0, p r1) i .r7 ?e'
  Pop $8
  ${If} $7 == 0
    Goto deleteRenameIdentityFailed
  ${EndIf}
  System::Call '*$1(i .r2, &v24, i .r3, &v12, i .r4, i .r5)'
  IntOp $7 $2 & ${FILE_ATTRIBUTE_DIRECTORY}
  ${If} $7 != 0
    Goto deleteRenameIdentityFailed
  ${EndIf}
  IntOp $7 $2 & ${FILE_ATTRIBUTE_REPARSE_POINT}
  ${If} $7 != 0
    Goto deleteRenameIdentityFailed
  ${EndIf}
  ${If} $4 == 0
  ${AndIf} $5 == 0
    Goto deleteRenameIdentityFailed
  ${EndIf}
  ${If} $3 != "$RenameExpectedVolumeSerial"
  ${OrIf} $4 != "$RenameExpectedFileIndexHigh"
  ${OrIf} $5 != "$RenameExpectedFileIndexLow"
    DetailPrint "A renamed-file path now refers to a different file identity and was retained: $RenameIdentityPath"
    Goto deleteRenameIdentityFailed
  ${EndIf}

  System::Call '*(&i1 1) p .r6'
  ${If} $6 == 0
    Goto deleteRenameIdentityFailed
  ${EndIf}
  System::Call 'kernel32::SetFileInformationByHandle(p r0, i ${FILE_DISPOSITION_INFO_CLASS}, p r6, i 1) i .r7 ?e'
  Pop $8
  ${If} $7 == 0
    Goto deleteRenameIdentityFailed
  ${EndIf}

  System::Free $6
  System::Free $1
  Call CloseRenameIdentityHandle
  StrCpy $RenameIdentityFailed "0"
  Return

  deleteRenameIdentityFailed:
  ${If} $6 != 0
    System::Free $6
  ${EndIf}
  ${If} $1 != 0
    System::Free $1
  ${EndIf}
  Call CloseRenameIdentityHandle
FunctionEnd

Function FlushRenameCleanupJournal
  StrCpy $InstallRecoveryFailed "0"
  !if ${LIBPATH} != "lib32"
    System::Call 'advapi32::RegOpenKeyExW(p ${WIN32_HKEY_LOCAL_MACHINE}, w "${INSTALLER_APP_RECOVERY_REGPATH}", i 0, i ${KEY_QUERY_VALUE_64}, *p .r0) i .r1'
  !else
    System::Call 'advapi32::RegOpenKeyExW(p ${WIN32_HKEY_LOCAL_MACHINE}, w "${INSTALLER_APP_RECOVERY_REGPATH}", i 0, i ${KEY_QUERY_VALUE}, *p .r0) i .r1'
  !endif
  ${If} $1 != 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  System::Call 'advapi32::RegFlushKey(p r0) i .r1'
  System::Call 'advapi32::RegCloseKey(p r0) i .r2'
  ${If} $1 != 0
  ${OrIf} $2 != 0
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
FunctionEnd

Function PersistInstallRecoveryJournalVersion
  StrCpy $InstallRecoveryFailed "0"
  ClearErrors
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "JournalVersion" ${INSTALL_RECOVERY_JOURNAL_VERSION}
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "JournalVersion"
  ${If} ${Errors}
  ${OrIf} $0 != ${INSTALL_RECOVERY_JOURNAL_VERSION}
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
FunctionEnd

Function RetireLegacyRenameManifest
  ; v3.0.2 overwrote the start of this manifest on every append. Its remaining
  ; path text cannot prove ownership, so never infer or delete any .old file from
  ; it. Retire only the exact protected manifest and preserve all application
  ; files; committed/rollback-cleanup are already irreversible decisions.
  StrCpy $InstallRecoveryFailed "0"
  ClearErrors
  ReadRegStr $InstallRecoveryPhase HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${ElseIf} $InstallRecoveryPhase != "committed"
  ${AndIf} $InstallRecoveryPhase != "rollback-cleanup"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  Call SecureExistingInstallRecoveryTree
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ProductRootCreated"
  ${If} ${Errors}
  ${OrIf} $0 != 1
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  StrCpy $LegacyRenameRepairPath "$InstallRollbackDirectory\renamed-files.v2"
  !insertmacro RetireLegacyRenameRecoveryArtifact "$LegacyRenameRepairPath"
  !insertmacro RetireLegacyRenameRecoveryArtifact "$RenameManifestPath"
  Call PersistInstallRecoveryJournalVersion
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  ClearErrors
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "RenameCleanupStarted" 1
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "RenameCleanupStarted"
  ${If} ${Errors}
  ${OrIf} $0 != 1
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  DetailPrint "Retired the corrupted v3.0.2 rename manifest; unprovable .old files were retained."
  Return

  retireLegacyRenameManifestFailed:
  StrCpy $InstallRecoveryFailed "1"
FunctionEnd

Function PrepareRenameManifestForCleanup
  StrCpy $InstallRecoveryFailed "0"
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "JournalVersion"
  ${If} ${Errors}
    ; v3.0.2 did not write a journal version and used the broken append logic.
    Call RetireLegacyRenameManifest
    Return
  ${EndIf}
  ${If} $0 != ${INSTALL_RECOVERY_JOURNAL_VERSION}
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
FunctionEnd

Function ClearInstallRecoveryJournal
  ; This is always the final cleanup step. Snapshot and rename-manifest cleanup
  ; must finish first so a crash never leaves filesystem state without a journal.
  StrCpy $InstallRecoveryFailed "0"
  DeleteRegKey HKLM ${INSTALLER_APP_RECOVERY_REGPATH}
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Pending"
  ${IfNot} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  DeleteRegKey /ifempty HKLM ${REGPATH}
FunctionEnd

Function DiscardInstallRecoverySnapshot
  Call CloseRenameManifest
  ; This is the only recursive cleanup target. It is reconstructed from a fixed
  ; Common AppData path, secured against non-admin replacement, and revalidated
  ; immediately before RMDir /r. Never call this for an unjournaled orphan.
  Call InitializeInstallRecoveryPaths
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  Call SecureExistingInstallRecoveryTree
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
  ${OrIf} $InstallRecoveryPathExists == "0"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackDirectory"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  ${If} $InstallRecoveryPathExists == "0"
    ; Recursive child cleanup already finished. Because ProductRootCreated=1 was
    ; verified by SecureExistingInstallRecoveryTree, the empty secured parent is
    ; transaction-owned and may be removed non-recursively.
    StrCpy $InstallRecoveryAclTarget "$InstallRecoveryProductRoot"
    Call HardenInstallRecoveryDirectory
    ${If} $InstallRecoveryFailed == "1"
      Return
    ${EndIf}
    RMDir "$InstallRecoveryProductRoot"
    StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
    StrCpy $InstallRecoveryPathRequired "0"
    Call ValidateInstallRecoveryComponent
    ${If} $InstallRecoveryFailed == "1"
    ${OrIf} $InstallRecoveryPathExists == "1"
      StrCpy $InstallRecoveryFailed "1"
    ${EndIf}
    Return
  ${EndIf}
  Call ValidateInstallRecoveryComponents
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  RMDir /r "$InstallRollbackDirectory"
  StrCpy $InstallRecoveryPathToCheck "$InstallRollbackDirectory"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
  ${OrIf} $InstallRecoveryPathExists == "1"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  RMDir "$InstallRecoveryProductRoot"
  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
  ${OrIf} $InstallRecoveryPathExists == "1"
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
FunctionEnd

Function LoadInstallRecoveryTargetFromJournal
  StrCpy $InstallRecoveryFailed "0"
  ClearErrors
  ReadRegStr $INSTDIR HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "InstallPath"
  ${If} ${Errors}
  ${OrIf} $INSTDIR == ""
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  Call ValidateInstallRecoveryTarget
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  ; New journals always persist this before Pending. Recovery must not silently
  ; downgrade to a lexical-only legacy target when the value is missing.
  ClearErrors
  ReadRegStr $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PhysicalInstallPath"
  ${If} ${Errors}
  ${OrIf} $0 == ""
  ${OrIf} $0 != "$InstallRecoveryPhysicalInstallPath"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
FunctionEnd

Function CleanupCompletedInstallTransaction
  ; committed and rollback-cleanup are decision phases: recovery may only finish
  ; deleting transaction-owned .old entries and the secure snapshot, then clear
  ; the journal. It must never replay installation or rollback mutations.
  Call LoadInstallRecoveryTargetFromJournal
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  Call PrepareRenameManifestForCleanup
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  Call DiscardRenamedProductFiles
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  Call DiscardInstallRecoverySnapshot
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  Call ClearInstallRecoveryJournal
FunctionEnd

Function RecoverInstallTransaction
  StrCpy $InstallRecoveryFailed "0"
  StrCpy $InstallRollbackState "0"
  StrCpy $RenameManifestHandle ""
  Call InitializeInstallRecoveryPaths
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  Call ValidateInstallRecoveryComponents
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}

  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Pending"
  ${If} ${Errors}
    ; A fixed name is not proof of ownership. Never recursively delete an
    ; unjournaled directory; preserve it for explicit administrator inspection.
    StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
    StrCpy $InstallRecoveryPathRequired "0"
    Call ValidateInstallRecoveryComponent
    ${If} $InstallRecoveryFailed == "1"
      Return
    ${ElseIf} $InstallRecoveryPathExists == "1"
      DetailPrint "An unjournaled installer recovery tree was found and retained: $InstallRecoveryProductRoot"
      StrCpy $InstallRecoveryFailed "1"
      Return
    ${EndIf}
    ; A partial registry journal created before Pending is safe to discard only
    ; when no filesystem recovery tree exists.
    Call ClearInstallRecoveryJournal
    Return
  ${EndIf}
  ${If} $0 != 1
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ClearErrors
  ReadRegStr $InstallRecoveryPhase HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ${If} $InstallRecoveryPhase == "initializing"
  ${OrIf} $InstallRecoveryPhase == "preparing"
  ${OrIf} $InstallRecoveryPhase == "prepared"
    ; No product mutation is allowed until the active phase is durably written.
    Call DiscardInstallRecoverySnapshot
    ${If} $InstallRecoveryFailed == "1"
      Return
    ${EndIf}
    Call ClearInstallRecoveryJournal
    Return
  ${ElseIf} $InstallRecoveryPhase == "committed"
    Call CleanupCompletedInstallTransaction
    Return
  ${ElseIf} $InstallRecoveryPhase == "rollback-cleanup"
    Call CleanupCompletedInstallTransaction
    Return
  ${ElseIf} $InstallRecoveryPhase != "active"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  Call ValidateActiveInstallRecoverySnapshot
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  Call LoadInstallRecoveryTargetFromJournal
  ${If} $InstallRecoveryFailed == "1"
    Return
  ${EndIf}
  ReadRegDWORD $PreviousApoPresent HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousApoPresent"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ReadRegDWORD $NewApoRegistrationAttempted HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "NewApoRegistrationAttempted"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ${If} $PreviousApoPresent != 0
  ${AndIf} $PreviousApoPresent != 1
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ${If} $NewApoRegistrationAttempted != 0
  ${AndIf} $NewApoRegistrationAttempted != 1
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  StrCpy $InstallRollbackState "1"
  Call RollbackInstallTransaction
  ${If} $InstallRollbackState != "0"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  DetailPrint "Recovered the application tree from an interrupted installation."
FunctionEnd

Function RemoveInstalledProductFiles
  ; Current payload. Keep config and VSTPlugins intact even on a fresh-install
  ; failure: an existing folder may already contain user-created data.
  !insertmacro DeleteTransactionFile "$INSTDIR\EqualizerAPO.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\DeviceSelector.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\Benchmark.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\VoicemeeterClient.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\UpdateChecker.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\Editor.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\Uninstall.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\NOTICE.md"
  !insertmacro DeleteTransactionFile "$INSTDIR\LICENSE.txt"
  !insertmacro DeleteTransactionFile "$INSTDIR\libfftw3.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\fftw3.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\sndfile.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\FLAC.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\libmp3lame.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\mpg123.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\ogg.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\opus.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\vorbis.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\vorbisenc.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\vorbisfile.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\msvcp140.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\msvcp140_1.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\vcruntime140.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\vcruntime140_1.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\d3dcompiler_47.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\icuuc.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\Qt6Core.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\Qt6Gui.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\Qt6Network.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\Qt6Svg.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\Qt6Widgets.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\Configuration tutorial (online).url"
  !insertmacro DeleteTransactionFile "$INSTDIR\Configuration reference (online).url"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt.conf"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\generic\qtuiotouchplugin.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\iconengines\qsvgicon.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\imageformats\qico.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\imageformats\qsvg.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\networkinformation\qnetworklistmanager.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\platforms\qwindows.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\styles\qmodernwindowsstyle.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\tls\qcertonlybackend.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\tls\qschannelbackend.dll"

  ; Files retired by this release may have been removed or renamed before the
  ; failure. The snapshot restores them for an upgrade.
  !insertmacro DeleteTransactionFile "$INSTDIR\Configurator.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\Qt5Core.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\Qt5Gui.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\Qt5Widgets.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\libfftw3-3.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\libsndfile-1.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\msvcp100.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\msvcr100.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\msvcp120.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\msvcr120.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\msvcp140_2.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\icudt.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\icuin.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\icudt78.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\icuin78.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\icuuc78.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\imageformats\qgif.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\imageformats\qjpeg.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\qt\styles\qwindowsvistastyle.dll"
  ; Remove only empty product directories. Never recursively delete a directory
  ; which may contain an unrelated file or a reparse point.
  RMDir "$INSTDIR\qt\generic"
  RMDir "$INSTDIR\qt\iconengines"
  RMDir "$INSTDIR\qt\imageformats"
  RMDir "$INSTDIR\qt\networkinformation"
  RMDir "$INSTDIR\qt\platforms"
  RMDir "$INSTDIR\qt\styles"
  RMDir "$INSTDIR\qt\tls"
  RMDir "$INSTDIR\qt"
FunctionEnd

Function CloseRenameManifest
  ${If} $RenameManifestHandle != ""
    FileClose $RenameManifestHandle
    StrCpy $RenameManifestHandle ""
  ${EndIf}
FunctionEnd

Function DiscardRenamedProductFiles
  Call CloseRenameManifest
  StrCpy $InstallRecoveryFailed "0"

  ; Delete only exact paths recorded after successful Rename calls. Cleanup gets
  ; one durable attempt: after any crash/failure, the manifest is retired without
  ; another product-file deletion so a replacement path can never be adopted.
  System::Call 'kernel32::GetFileAttributesW(w "$RenameManifestPath") i .r0 ?e'
  Pop $1
  ${If} $0 == ${INVALID_FILE_ATTRIBUTES}
    ${If} $1 == ${ERROR_FILE_NOT_FOUND}
    ${OrIf} $1 == ${ERROR_PATH_NOT_FOUND}
      Return
    ${EndIf}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  IntOp $1 $0 & ${FILE_ATTRIBUTE_REPARSE_POINT}
  ${If} $1 != 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  IntOp $1 $0 & ${FILE_ATTRIBUTE_DIRECTORY}
  ${If} $1 != 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  ClearErrors
  ReadRegDWORD $2 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "RenameCleanupStarted"
  ${If} ${Errors}
  ${OrIf} $2 != 0
    DetailPrint "Renamed-file cleanup was already attempted or its state is unavailable; retaining application files."
    Goto retireRenameManifest
  ${EndIf}
  ClearErrors
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "RenameCleanupStarted" 1
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ClearErrors
  ReadRegDWORD $2 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "RenameCleanupStarted"
  ${If} ${Errors}
  ${OrIf} $2 != 1
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  Call FlushRenameCleanupJournal
  ${If} $InstallRecoveryFailed == "1"
    DetailPrint "Renamed-file cleanup was not started because its durable marker could not be flushed."
    Return
  ${EndIf}

  ClearErrors
  GetFullPathName $5 "$INSTDIR"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  StrCpy $6 "$5\"
  ClearErrors
  FileOpen $0 "$RenameManifestPath" r
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  readRenamedProductFile:
  ClearErrors
  FileReadUTF16LE $0 $1
  ${If} ${Errors}
    Goto closeAndRetireRenameManifest
  ${EndIf}
  ${StrTrimNewLines} $1 "$1"
  ${If} $1 != ""
    ; Confirmation records include the source file identity and are framed at
    ; both ends. A partial append after power loss is never interpreted as a path.
    ${StrTok} $3 "$1" "|" "0" "0"
    ${StrTok} $RenameExpectedVolumeSerial "$1" "|" "1" "0"
    ${StrTok} $RenameExpectedFileIndexHigh "$1" "|" "2" "0"
    ${StrTok} $RenameExpectedFileIndexLow "$1" "|" "3" "0"
    ${StrTok} $RenameIdentityPath "$1" "|" "4" "0"
    ${StrTok} $7 "$1" "|" "5" "0"
    ${StrTok} $8 "$1" "|" "6" "0"
    ${If} $3 != "C"
    ${OrIf} $7 != "C"
    ${OrIf} $8 != ""
    ${OrIf} $RenameExpectedVolumeSerial == ""
    ${OrIf} $RenameExpectedFileIndexHigh == ""
    ${OrIf} $RenameExpectedFileIndexLow == ""
    ${OrIf} $RenameIdentityPath == ""
      DetailPrint "Retaining renamed files because the confirmation manifest is malformed."
      Goto closeAndRetireRenameManifest
    ${EndIf}

    ; Canonicalize the parent separately so a previously removed exact file is
    ; harmless. Lexical tricks such as .. cannot escape the validated tree.
    ${GetParent} "$RenameIdentityPath" $4
    ${GetFileName} "$RenameIdentityPath" $9
    ${If} $4 == ""
    ${OrIf} $9 == ""
      DetailPrint "Retaining renamed files because a confirmation path is incomplete."
      Goto closeAndRetireRenameManifest
    ${EndIf}
    ClearErrors
    GetFullPathName $4 "$4"
    ${If} ${Errors}
      DetailPrint "Retaining renamed files because a confirmation path could not be canonicalized."
      Goto closeAndRetireRenameManifest
    ${Else}
      StrCpy $4 "$4\$9"
      StrCpy $RenameIdentityPath "$4"
      StrLen $7 "$6"
      StrCpy $8 "$4" $7
      ${If} $8 == "$6"
        Call DeleteRenameFileByIdentity
        ${If} $RenameIdentityFailed == "1"
          DetailPrint "A confirmed renamed file could not be safely deleted and was retained: $4"
          Goto closeAndRetireRenameManifest
        ${EndIf}
      ${Else}
        DetailPrint "Refused an out-of-scope rename-manifest entry: $1"
        Goto closeAndRetireRenameManifest
      ${EndIf}
    ${EndIf}
  ${Else}
    DetailPrint "Retaining renamed files because the confirmation manifest contains a blank record."
    Goto closeAndRetireRenameManifest
  ${EndIf}
  Goto readRenamedProductFile

  closeAndRetireRenameManifest:
  FileClose $0

  retireRenameManifest:
  ClearErrors
  Delete "$RenameManifestPath"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
  ${EndIf}
FunctionEnd

Function PrepareInstallTransaction
  StrCpy $InstallRollbackState "0"
  StrCpy $PreviousApoPresent "0"
  StrCpy $NewApoRegistrationAttempted "0"
  StrCpy $RenameManifestHandle ""
  StrCpy $InstallRecoveryFailed "0"
  Call InitializeInstallRecoveryPaths
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallRollbackCopyCode "Common AppData recovery path resolution failed"
    Goto installBackupFailed
  ${EndIf}
  Call ValidateInstallRecoveryTarget
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallRollbackCopyCode "unsafe installation path"
    Goto installBackupFailed
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\EqualizerAPO.dll"
    StrCpy $PreviousApoPresent "1"
  ${EndIf}

  ; Never adopt or delete an unjournaled directory at the fixed recovery name.
  Call ValidateInstallRecoveryComponents
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallRollbackCopyCode "unsafe Common AppData recovery component"
    Goto installBackupFailed
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$InstallRecoveryProductRoot"
  StrCpy $InstallRecoveryPathRequired "0"
  Call ValidateInstallRecoveryComponent
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallRollbackCopyCode "recovery product-root validation failed"
    Goto installBackupFailed
  ${ElseIf} $InstallRecoveryPathExists == "1"
    StrCpy $InstallRollbackCopyCode "an unjournaled recovery product root already exists"
    Goto installBackupFailed
  ${EndIf}

  ; Persist all metadata and Pending before creating filesystem artifacts. This
  ; makes even a power loss during directory creation recoverable next run.
  Call SaveInstallMetadataJournal
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallRollbackCopyCode "registry-state journal creation failed"
    Goto installBackupFailed
  ${EndIf}

  ClearErrors
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase" "initializing"
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "initial recovery phase journal creation failed"
    Goto installBackupFailed
  ${EndIf}
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Pending" 1
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "recovery pending marker creation failed"
    Goto installBackupFailed
  ${EndIf}
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Pending"
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "recovery pending marker verification failed"
    Goto installBackupFailed
  ${ElseIf} $0 != 1
    StrCpy $InstallRollbackCopyCode "recovery pending marker did not verify"
    Goto installBackupFailed
  ${EndIf}

  Call CreateSecureInstallRecoveryTree
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallRollbackCopyCode "secure recovery directory creation failed"
    Goto installBackupFailed
  ${EndIf}
  ClearErrors
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase" "preparing"
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "preparing recovery phase could not be written"
    Goto installBackupFailed
  ${EndIf}

  ; Export the exact task definition before UpdateChecker can mutate it. Presence
  ; alone is insufficient because trigger, principal, arguments and settings are
  ; all part of rollback state.
  nsExec::ExecToLog '"$SYSDIR\schtasks.exe" /Query /TN "EqualizerAPOUpdateChecker" /FO LIST'
  Pop $InstallOperationCode
  ${If} $InstallOperationCode == "error"
    StrCpy $InstallRollbackCopyCode "scheduled-task state query could not start"
    Goto installBackupFailed
  ${ElseIf} $InstallOperationCode == 0
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousUpdateTaskPresent" 1
    ${If} ${Errors}
      StrCpy $InstallRollbackCopyCode "scheduled-task presence journal creation failed"
      Goto installBackupFailed
    ${EndIf}
    nsExec::ExecToLog '"$SYSDIR\cmd.exe" /D /Q /S /C "$\"$SYSDIR\schtasks.exe$\" /Query /TN $\"EqualizerAPOUpdateChecker$\" /XML > $\"$InstallRecoveryTaskXmlPath$\""'
    Pop $InstallOperationCode
    ${If} $InstallOperationCode == "error"
      StrCpy $InstallRollbackCopyCode "scheduled-task XML export could not start"
      Goto installBackupFailed
    ${ElseIf} $InstallOperationCode != 0
      StrCpy $InstallRollbackCopyCode "scheduled-task XML export failed with exit code $InstallOperationCode"
      Goto installBackupFailed
    ${EndIf}
    System::Call 'kernel32::GetFileAttributesW(w "$InstallRecoveryTaskXmlPath") i .r0 ?e'
    Pop $2
    ${If} $0 == ${INVALID_FILE_ATTRIBUTES}
      StrCpy $InstallRollbackCopyCode "scheduled-task XML export is missing"
      Goto installBackupFailed
    ${EndIf}
    IntOp $1 $0 & ${FILE_ATTRIBUTE_REPARSE_POINT}
    ${If} $1 != 0
      StrCpy $InstallRollbackCopyCode "scheduled-task XML export is a reparse point"
      Goto installBackupFailed
    ${EndIf}
    ClearErrors
    FileOpen $0 "$InstallRecoveryTaskXmlPath" r
    ${If} ${Errors}
      StrCpy $InstallRollbackCopyCode "scheduled-task XML export could not be opened"
      Goto installBackupFailed
    ${EndIf}
    FileRead $0 $1
    FileClose $0
    ${StrTrimNewLines} $1 "$1"
    ${If} $1 == ""
      StrCpy $InstallRollbackCopyCode "scheduled-task XML export is empty"
      Goto installBackupFailed
    ${EndIf}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousUpdateTaskXmlSaved" 1
  ${ElseIf} $InstallOperationCode == 1
    ; schtasks also uses exit 1 for failures other than "not found". Accept an
    ; absent task only when its canonical Task Scheduler backing file is absent
    ; with a precise file/path-not-found Win32 result.
    System::Call 'kernel32::GetFileAttributesW(w "$WINDIR\System32\Tasks\EqualizerAPOUpdateChecker") i .r0 ?e'
    Pop $2
    ${If} $0 != ${INVALID_FILE_ATTRIBUTES}
      StrCpy $InstallRollbackCopyCode "scheduled-task query returned exit 1 but its backing file still exists"
      Goto installBackupFailed
    ${ElseIf} $2 != ${ERROR_FILE_NOT_FOUND}
    ${AndIf} $2 != ${ERROR_PATH_NOT_FOUND}
      StrCpy $InstallRollbackCopyCode "scheduled-task query returned exit 1 with Win32 error $2"
      Goto installBackupFailed
    ${EndIf}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousUpdateTaskPresent" 0
  ${Else}
    StrCpy $InstallRollbackCopyCode "scheduled-task state query failed with exit code $InstallOperationCode"
    Goto installBackupFailed
  ${EndIf}
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "scheduled-task state journal creation failed"
    Goto installBackupFailed
  ${EndIf}

  ; Preserve config ACLs because config is intentionally excluded from the file
  ; snapshot but the installer grants access recursively later.
  ${If} ${FileExists} "$INSTDIR\config"
    nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR\config" /save "$InstallRecoveryAclPath" /T /C /Q'
    Pop $InstallOperationCode
    ${If} $InstallOperationCode == "error"
      StrCpy $InstallRollbackCopyCode "config ACL snapshot could not start"
      Goto installBackupFailed
    ${ElseIf} $InstallOperationCode != 0
      StrCpy $InstallRollbackCopyCode "config ACL snapshot failed with exit code $InstallOperationCode"
      Goto installBackupFailed
    ${EndIf}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ConfigAclSaved" 1
  ${Else}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ConfigAclSaved" 0
  ${EndIf}
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "config ACL journal creation failed"
    Goto installBackupFailed
  ${EndIf}

  ; Snapshot the old application tree outside $INSTDIR before changing files.
  ; config and VSTPlugins are excluded because they are user-data containers and
  ; are never deleted or overwritten by rollback.
  Call ValidateInstallRecoveryComponents
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallRollbackCopyCode "recovery tree changed before application snapshot"
    Goto installBackupFailed
  ${EndIf}
  nsExec::ExecToLog '"$SYSDIR\robocopy.exe" "$INSTDIR" "$InstallRollbackFiles" /E /COPY:DATS /DCOPY:DAT /R:1 /W:1 /XJ /XD "$INSTDIR\config" "$INSTDIR\VSTPlugins" /NFL /NDL /NJH /NJS /NP'
  Pop $InstallRollbackCopyCode
  ${If} $InstallRollbackCopyCode == "error"
    Goto installBackupFailed
  ${ElseIf} $InstallRollbackCopyCode >= 8
    Goto installBackupFailed
  ${EndIf}
  Call ValidateInstallRecoveryComponents
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallRollbackCopyCode "recovery tree changed during application snapshot"
    Goto installBackupFailed
  ${EndIf}

  ClearErrors
  FileOpen $RenameManifestHandle "$RenameManifestPath" w
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "rename manifest creation failed"
    Goto installBackupFailed
  ${EndIf}
  FileClose $RenameManifestHandle
  StrCpy $RenameManifestHandle ""

  ClearErrors
  FileOpen $0 "$InstallRecoveryMarkerPath" w
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "recovery marker creation failed"
    Goto installBackupFailed
  ${EndIf}
  FileWrite $0 "EqualizerAPO installer app-tree recovery v1$\r$\n"
  ${If} ${Errors}
    FileClose $0
    StrCpy $InstallRollbackCopyCode "recovery marker write failed"
    Goto installBackupFailed
  ${EndIf}
  FileClose $0

  ClearErrors
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase" "prepared"
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "prepared recovery phase could not be written"
    Goto installBackupFailed
  ${EndIf}
  ; No application file may be changed until active is written and read back.
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase" "active"
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "active recovery phase could not be written"
    Goto installBackupFailed
  ${EndIf}
  ClearErrors
  ReadRegStr $InstallRecoveryPhase HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase"
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "active recovery phase could not be verified"
    Goto installBackupFailed
  ${ElseIf} $InstallRecoveryPhase != "active"
    StrCpy $InstallRollbackCopyCode "active recovery phase did not verify"
    Goto installBackupFailed
  ${EndIf}

  StrCpy $InstallRollbackState "1"
  DetailPrint "Application-file rollback snapshot created at $InstallRollbackDirectory."
  Return

  installBackupFailed:
  DetailPrint "Could not create the application-file rollback snapshot. Error: $InstallRollbackCopyCode"
  ${If} $RenameManifestHandle != ""
    FileClose $RenameManifestHandle
    StrCpy $RenameManifestHandle ""
  ${EndIf}
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Pending"
  ${IfNot} ${Errors}
  ${AndIf} $0 == 1
    Call DiscardInstallRecoverySnapshot
  ${Else}
    ; Without Pending, the fixed tree is not ours and must not be changed.
    StrCpy $InstallRecoveryFailed "0"
  ${EndIf}
  ${If} $InstallRecoveryFailed == "0"
    Call ClearInstallRecoveryJournal
  ${Else}
    DetailPrint "Unsafe or incomplete recovery artifacts were retained with their journal."
  ${EndIf}
  ${IfNot} ${Silent}
    MessageBox MB_ICONSTOP|MB_OK "The existing application files could not be backed up safely. Installation will stop before replacing them."
  ${EndIf}
  Abort
FunctionEnd

Function RollbackInstallTransaction
  ${If} $InstallRollbackState != "1"
    Return
  ${EndIf}

  Call InitializeInstallRecoveryPaths
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "The fixed recovery directory could not be resolved; the recovery journal was retained."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}

  ; Registry Phase is the durable transaction decision. Never let a stale
  ; process-local rollback flag undo a committed install or replay a completed
  ; rollback after a commit readback/cleanup failure.
  ClearErrors
  ReadRegStr $InstallRecoveryPhase HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase"
  ${If} ${Errors}
    StrCpy $ApoRollbackStatus "The durable installation phase is missing; no rollback mutations were attempted and the journal was retained."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}
  ${If} $InstallRecoveryPhase == "committed"
  ${OrIf} $InstallRecoveryPhase == "rollback-cleanup"
    StrCpy $InstallRollbackState "0"
    Call CleanupCompletedInstallTransaction
    ${If} $InstallRecoveryFailed == "1"
      StrCpy $ApoRollbackStatus "The installation decision was already durable; cleanup is deferred and no rollback mutations were attempted."
    ${Else}
      StrCpy $ApoRollbackStatus "The installation decision was already durable; transaction cleanup completed without rollback."
    ${EndIf}
    DetailPrint "$ApoRollbackStatus"
    Return
  ${ElseIf} $InstallRecoveryPhase != "active"
    StrCpy $ApoRollbackStatus "The durable installation phase is not active; no rollback mutations were attempted and the journal was retained."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}

  Call ValidateActiveInstallRecoverySnapshot
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "The active recovery snapshot is missing, incomplete, or unsafe; no rollback mutations were attempted and the journal was retained."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}
  DetailPrint "Installation did not commit; restoring the pre-install application files and metadata."
  StrCpy $ApoRollbackRegistrationCode "not attempted"
  StrCpy $ApoRollbackStatus "The new application files were removed. User configuration was preserved."
  Call CloseRenameManifest

  ; Restore the exact pre-install task definition without relying on a helper
  ; executable which may itself be replaced or removed during rollback.
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "UpdaterOperationStarted"
  ${If} ${Errors}
    StrCpy $ApoRollbackStatus "Updater recovery state is missing. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${ElseIf} $0 == 1
    ReadRegDWORD $1 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousUpdateTaskPresent"
    ${If} ${Errors}
      StrCpy $ApoRollbackStatus "Previous updater-task state is missing. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    ClearErrors
    ReadRegDWORD $2 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "PreviousUpdateTaskXmlSaved"
    ${If} ${Errors}
      StrCpy $ApoRollbackStatus "Previous updater-task XML state is missing. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    ${If} $1 == 1
      ${If} $2 != 1
        StrCpy $ApoRollbackStatus "Previous updater-task XML state is malformed. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
      System::Call 'kernel32::GetFileAttributesW(w "$InstallRecoveryTaskXmlPath") i .r3 ?e'
      Pop $4
      ${If} $3 == ${INVALID_FILE_ATTRIBUTES}
        StrCpy $ApoRollbackStatus "The previous updater-task XML is missing. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
      IntOp $5 $3 & ${FILE_ATTRIBUTE_REPARSE_POINT}
      ${If} $5 != 0
        StrCpy $ApoRollbackStatus "The previous updater-task XML is unsafe. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
      nsExec::ExecToLog '"$SYSDIR\schtasks.exe" /Create /TN "EqualizerAPOUpdateChecker" /XML "$InstallRecoveryTaskXmlPath" /F'
      Pop $InstallOperationCode
      ${If} $InstallOperationCode == "error"
        StrCpy $ApoRollbackStatus "The exact previous updater task could not be recreated. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${ElseIf} $InstallOperationCode != 0
        StrCpy $ApoRollbackStatus "The exact previous updater task could not be recreated (exit code: $InstallOperationCode). Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
    ${ElseIf} $1 == 0
      ${If} $2 != 0
        StrCpy $ApoRollbackStatus "The absent updater-task XML state is malformed. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
      nsExec::ExecToLog '"$SYSDIR\schtasks.exe" /Query /TN "EqualizerAPOUpdateChecker" /FO LIST'
      Pop $InstallOperationCode
      ${If} $InstallOperationCode == "error"
        StrCpy $ApoRollbackStatus "The updater task could not be queried during rollback. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${ElseIf} $InstallOperationCode == 0
        nsExec::ExecToLog '"$SYSDIR\schtasks.exe" /Delete /TN "EqualizerAPOUpdateChecker" /F'
        Pop $InstallOperationCode
        ${If} $InstallOperationCode == "error"
          StrCpy $ApoRollbackStatus "The newly created updater task could not be deleted. Recovery files remain at $InstallRollbackDirectory."
          DetailPrint "$ApoRollbackStatus"
          Return
        ${ElseIf} $InstallOperationCode != 0
          StrCpy $ApoRollbackStatus "The newly created updater task could not be deleted (exit code: $InstallOperationCode). Recovery files remain at $InstallRollbackDirectory."
          DetailPrint "$ApoRollbackStatus"
          Return
        ${EndIf}
      ${ElseIf} $InstallOperationCode == 1
        System::Call 'kernel32::GetFileAttributesW(w "$WINDIR\System32\Tasks\EqualizerAPOUpdateChecker") i .r3 ?e'
        Pop $4
        ${If} $3 != ${INVALID_FILE_ATTRIBUTES}
          StrCpy $ApoRollbackStatus "The updater task query returned exit 1 but its backing file still exists. Recovery files remain at $InstallRollbackDirectory."
          DetailPrint "$ApoRollbackStatus"
          Return
        ${ElseIf} $4 != ${ERROR_FILE_NOT_FOUND}
        ${AndIf} $4 != ${ERROR_PATH_NOT_FOUND}
          StrCpy $ApoRollbackStatus "The updater task query returned exit 1 with Win32 error $4. Recovery files remain at $InstallRollbackDirectory."
          DetailPrint "$ApoRollbackStatus"
          Return
        ${EndIf}
      ${ElseIf} $InstallOperationCode != 1
        StrCpy $ApoRollbackStatus "The updater task query failed during rollback (exit code: $InstallOperationCode). Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
    ${Else}
      StrCpy $ApoRollbackStatus "Previous updater-task state is malformed. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "UpdaterOperationStarted" 0
    ${If} ${Errors}
      StrCpy $ApoRollbackStatus "The updater task was restored, but its rollback phase could not be persisted. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    ClearErrors
    ReadRegDWORD $3 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "UpdaterOperationStarted"
    ${If} ${Errors}
    ${OrIf} $3 != 0
      StrCpy $ApoRollbackStatus "The updater rollback phase did not verify. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
  ${ElseIf} $0 != 0
    StrCpy $ApoRollbackStatus "Updater recovery state is malformed. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}

  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "DeviceSelectorOperationStarted"
  ${If} ${Errors}
    StrCpy $ApoRollbackStatus "Device-selector recovery state is missing. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${ElseIf} $0 == 1
    ReadRegDWORD $1 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "DeviceSelectorMode"
    ${If} ${Errors}
      StrCpy $ApoRollbackStatus "Device-selector recovery mode is missing. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    ; Mode 2 is accepted only to finish a journal written by an older installer.
    ; New interactive selection is post-commit and never enters this transaction.
    ${If} $1 == 2
    ${AndIf} $PreviousApoPresent == 0
      ClearErrors
      StrCpy $InstallOperationCode "process did not start"
      ExecWait '"$INSTDIR\DeviceSelector.exe" /u /s' $InstallOperationCode
      ${If} ${Errors}
        StrCpy $ApoRollbackStatus "Fresh endpoint registration cleanup could not be started. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${ElseIf} $InstallOperationCode != 0
        StrCpy $ApoRollbackStatus "Fresh endpoint registration cleanup failed (exit code: $InstallOperationCode). Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
    ${ElseIf} $1 != 1
    ${AndIf} $1 != 2
      StrCpy $ApoRollbackStatus "Device-selector recovery mode is malformed. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "DeviceSelectorOperationStarted" 0
    ${If} ${Errors}
      StrCpy $ApoRollbackStatus "Device-selector rollback completed, but its phase could not be persisted. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    ClearErrors
    ReadRegDWORD $2 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "DeviceSelectorOperationStarted"
    ${If} ${Errors}
    ${OrIf} $2 != 0
      StrCpy $ApoRollbackStatus "Device-selector rollback phase did not verify. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
  ${ElseIf} $0 != 0
    StrCpy $ApoRollbackStatus "Device-selector recovery state is malformed. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}

  ; Only a regsvr32 process which actually started can have left partial COM
  ; entries. Pre-registration extraction/verification failures must never
  ; unregister the previously installed APO.
  ${If} $NewApoRegistrationAttempted == "1"
    ${If} ${FileExists} "$INSTDIR\EqualizerAPO.dll"
      ClearErrors
      StrCpy $ApoRollbackUnregistrationCode "process did not start"
      ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\EqualizerAPO.dll"' $ApoRollbackUnregistrationCode
      ${If} ${Errors}
        StrCpy $ApoRollbackStatus "Automatic rollback could not start regsvr32 to remove partial registration. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${ElseIf} $ApoRollbackUnregistrationCode != 0
        StrCpy $ApoRollbackStatus "Automatic rollback could not remove partial registration (regsvr32 exit code: $ApoRollbackUnregistrationCode). Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
    ${EndIf}
    StrCpy $NewApoRegistrationAttempted "0"
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "NewApoRegistrationAttempted" 0
    ${If} ${Errors}
      StrCpy $ApoRollbackStatus "The new APO was unregistered, but its recovery phase could not be persisted. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
  ${EndIf}

  StrCpy $InstallRecoveryFailed "0"
  Call RemoveInstalledProductFiles
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "Automatic rollback could not remove all new application files. User configuration was preserved and recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}

  Call ValidateInstallRecoveryComponents
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "The recovery tree changed before application files could be restored. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}
  nsExec::ExecToLog '"$SYSDIR\robocopy.exe" "$InstallRollbackFiles" "$INSTDIR" /E /COPY:DATS /DCOPY:DAT /R:1 /W:1 /XJ /NFL /NDL /NJH /NJS /NP'
  Pop $InstallRollbackCopyCode
  ${If} $InstallRollbackCopyCode == "error"
    StrCpy $ApoRollbackStatus "Automatic rollback could not restore all previous application files. User configuration was preserved and recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${ElseIf} $InstallRollbackCopyCode >= 8
    StrCpy $ApoRollbackStatus "Automatic rollback could not restore all previous application files (robocopy exit code: $InstallRollbackCopyCode). User configuration was preserved and recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}

  ${If} $PreviousApoPresent == "1"
    ${IfNot} ${FileExists} "$INSTDIR\EqualizerAPO.dll"
      StrCpy $ApoRollbackStatus "Previous application files were copied back, but EqualizerAPO.dll is missing. User configuration was preserved and recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}

    Call BeginProtectedAudioOverride
    ${If} $ProtectedAudioOverrideActive != "1"
      StrCpy $ApoRollbackStatus "The previous application files were restored, but the protected-audio recovery journal could not be created. User configuration was preserved and recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    ClearErrors
    StrCpy $ApoRollbackRegistrationCode "process did not start"
    ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\EqualizerAPO.dll"' $ApoRollbackRegistrationCode
    ${If} ${Errors}
      Call RestoreProtectedAudioSetting
      StrCpy $ApoRollbackStatus "The previous application files were restored, but regsvr32 could not be started to re-register the previous APO. User configuration was preserved and recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    Call RestoreProtectedAudioSetting
    ${If} $ProtectedAudioOverrideActive == "1"
      StrCpy $ApoRollbackStatus "The previous application files and APO registration were restored, but the protected-audio setting could not be restored. User configuration was preserved and recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${ElseIf} $ApoRollbackRegistrationCode != 0
      StrCpy $ApoRollbackStatus "The previous application files were restored, but APO re-registration failed (regsvr32 exit code: $ApoRollbackRegistrationCode). User configuration was preserved and recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
    StrCpy $ApoRollbackStatus "The previous application files were restored and the previous APO was re-registered. User configuration was preserved."

    ; Make the restored APO active before declaring rollback complete.
    ${If} ${FileExists} "$INSTDIR\DeviceSelector.exe"
      ClearErrors
      StrCpy $InstallOperationCode "process did not start"
      ExecWait '"$INSTDIR\DeviceSelector.exe" /r /s' $InstallOperationCode
      ${If} ${Errors}
        StrCpy $ApoRollbackStatus "The previous APO was restored, but the audio service restart could not be started. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${ElseIf} $InstallOperationCode != 0
        StrCpy $ApoRollbackStatus "The previous APO was restored, but the audio service restart failed (exit code: $InstallOperationCode). Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
    ${EndIf}
  ${Else}
    StrCpy $ApoRollbackStatus "The new application files were removed. User configuration was preserved."
    ; Keep the now-empty root until the durable journal is cleared. Recovery on
    ; the next run must still be able to resolve and verify its physical target.
  ${EndIf}

  ; Restore the ACL for the user-data directory which was deliberately excluded
  ; from the app-tree copy.
  ClearErrors
  ReadRegDWORD $0 HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "ConfigAclSaved"
  ${If} ${Errors}
    StrCpy $ApoRollbackStatus "Application files were restored, but the config ACL recovery state is missing. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${ElseIf} $0 == 1
    nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR" /restore "$InstallRecoveryAclPath" /C /Q'
    Pop $InstallOperationCode
    ${If} $InstallOperationCode == "error"
      StrCpy $ApoRollbackStatus "Application files were restored, but config ACL restoration could not start. Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${ElseIf} $InstallOperationCode != 0
      StrCpy $ApoRollbackStatus "Application files were restored, but config ACL restoration failed (exit code: $InstallOperationCode). Recovery files remain at $InstallRollbackDirectory."
      DetailPrint "$ApoRollbackStatus"
      Return
    ${EndIf}
  ${ElseIf} $0 == 0
    ${If} ${FileExists} "$INSTDIR\config"
      nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR\config" /reset /T /C /Q'
      Pop $InstallOperationCode
      ${If} $InstallOperationCode == "error"
        StrCpy $ApoRollbackStatus "Application files were restored, but the new config ACL could not be reset. Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${ElseIf} $InstallOperationCode != 0
        StrCpy $ApoRollbackStatus "Application files were restored, but the new config ACL reset failed (exit code: $InstallOperationCode). Recovery files remain at $InstallRollbackDirectory."
        DetailPrint "$ApoRollbackStatus"
        Return
      ${EndIf}
    ${EndIf}
  ${Else}
    StrCpy $ApoRollbackStatus "Application files were restored, but the config ACL recovery state is malformed. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}

  Call RestoreInstallMetadata
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "Application files were restored, but previous installer registry metadata could not be restored. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}
  Call RestoreInstallShortcuts
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "Application files and registry metadata were restored, but previous shortcuts could not be restored. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}

  DetailPrint "$ApoRollbackStatus"
  ; Persist the rollback decision before cleanup. A crash from this point onward
  ; resumes manifest/snapshot cleanup and never replays system mutations.
  ClearErrors
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase" "rollback-cleanup"
  ${If} ${Errors}
    StrCpy $ApoRollbackStatus "Rollback completed, but its cleanup phase could not be persisted. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}
  ClearErrors
  ReadRegStr $InstallRecoveryPhase HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase"
  ${If} ${Errors}
  ${OrIf} $InstallRecoveryPhase != "rollback-cleanup"
    StrCpy $ApoRollbackStatus "Rollback completed, but its cleanup phase did not verify. Recovery files remain at $InstallRollbackDirectory."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}
  StrCpy $InstallRollbackState "0"
  Call PrepareRenameManifestForCleanup
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "Rollback completed; rename-manifest migration is deferred to the next installer run. Recovery files and journal were retained."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}
  Call DiscardRenamedProductFiles
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "Rollback completed; renamed-file cleanup is deferred to the next installer run. Recovery files and journal were retained."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}
  Call DiscardInstallRecoverySnapshot
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "Rollback completed; snapshot cleanup is deferred to the next installer run. Recovery files and journal were retained."
    DetailPrint "$ApoRollbackStatus"
    Return
  ${EndIf}
  Call ClearInstallRecoveryJournal
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $ApoRollbackStatus "Rollback completed, but the durable recovery journal could not be cleared. A later installer run will retry cleanup."
    DetailPrint "$ApoRollbackStatus"
  ${EndIf}
FunctionEnd

Function CommitInstallTransaction
  ; The committed phase is the atomic decision point. It is written only after
  ; every fallible transactional operation, including silent service restart and
  ; updater work. Interactive endpoint selection is a post-commit user action.
  StrCpy $InstallRecoveryFailed "0"
  ClearErrors
  WriteRegStr HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase" "committed"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  ClearErrors
  ReadRegStr $InstallRecoveryPhase HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "Phase"
  ${If} ${Errors}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${ElseIf} $InstallRecoveryPhase != "committed"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  Call CloseRenameManifest
  StrCpy $NewApoRegistrationAttempted "0"
  StrCpy $InstallRollbackState "0"
  ; Cleanup is restartable and journal-last. A cleanup failure after committed
  ; cannot turn a successful install into rollback; the next run resumes it.
  Call CleanupCompletedInstallTransaction
  ${If} $InstallRecoveryFailed == "1"
    DetailPrint "Installation committed; transaction cleanup was deferred to the next installer run."
    StrCpy $InstallRecoveryFailed "0"
  ${EndIf}
  DetailPrint "Installation transaction committed."
FunctionEnd

Function .onInstFailed
  ; Also covers extraction/verification failures before the registration check.
  Call RollbackInstallTransaction
FunctionEnd

Function VerifyRequiredAssets
  !insertmacro RequireInstalledAsset "$INSTDIR\EqualizerAPO.dll"
  !insertmacro RequireInstalledAsset "$INSTDIR\DeviceSelector.exe"
  !insertmacro RequireInstalledAsset "$INSTDIR\Benchmark.exe"
  !insertmacro RequireInstalledAsset "$INSTDIR\VoicemeeterClient.exe"
  !insertmacro RequireInstalledAsset "$INSTDIR\UpdateChecker.exe"
  !insertmacro RequireInstalledAsset "$INSTDIR\Editor.exe"
  !insertmacro RequireInstalledAsset "$INSTDIR\NOTICE.md"
  !insertmacro RequireInstalledAsset "$INSTDIR\LICENSE.txt"
  !insertmacro RequireInstalledAsset "$INSTDIR\libfftw3.dll"
  !insertmacro RequireInstalledAsset "$INSTDIR\sndfile.dll"
  !insertmacro RequireInstalledAsset "$INSTDIR\Qt6Core.dll"
  !insertmacro RequireInstalledAsset "$INSTDIR\Qt6Gui.dll"
  !insertmacro RequireInstalledAsset "$INSTDIR\Qt6Network.dll"
  !insertmacro RequireInstalledAsset "$INSTDIR\Qt6Widgets.dll"
  !insertmacro RequireInstalledAsset "$INSTDIR\qt\platforms\qwindows.dll"
  !insertmacro RequireInstalledAsset "$INSTDIR\qt.conf"
  !insertmacro RequireInstalledAsset "$INSTDIR\config\config.txt"
  Return

  missingRequiredAsset:
  DetailPrint "Required installer asset is missing: $MissingAsset"
  ${IfNot} ${Silent}
    MessageBox MB_ICONSTOP|MB_OK $(AssetValidationError)
  ${EndIf}
  Call RollbackInstallTransaction
  Abort
FunctionEnd

;--------------------------------
;Installer Sections
LangString SecCheckForUpdates ${LANG_ENGLISH} "Check for updates automatically"
LangString SecCheckForUpdates ${LANG_SPANISH} "Buscar actualizaciones automaticamente"
LangString SecCheckForUpdates ${LANG_TRADCHINESE} "自動檢查更新"
LangString SecCheckForUpdates ${LANG_SIMPCHINESE} "自动检查更新"
LangString SecCheckForUpdates ${LANG_GERMAN} "Automatisch auf Updates prüfen"

Section /o $(SecCheckForUpdates) SecCheckForUpdates
SectionEnd

Section "-Install"
  SetOutPath "$INSTDIR"
  Call CreateRestorePoint
  Call CloseRunningApplications
  ; Read both shortcut locations before the durable journal is created so they
  ; can be restored after an interrupted or failed upgrade.
  !insertmacro MUI_STARTMENU_GETFOLDER Application $OldStartMenuFolder

  ;Migrate the versioned default folder while preserving custom folder names.
  StrCpy $0 "$OldStartMenuFolder" 14
  ${If} $0 == "Equalizer APO "
    StrCpy $StartMenuFolder "${PRODUCT_LABEL} ${VERSION}"
  ${ElseIf} $StartMenuFolder == ""
    StrCpy $StartMenuFolder "${PRODUCT_LABEL} ${VERSION}"
  ${EndIf}

  ; Snapshot every installed application file and relevant metadata before
  ; deleting or replacing anything.
  Call PrepareInstallTransaction

  Delete "$INSTDIR\Configurator.exe"
  Delete "$INSTDIR\Qt5Core.dll"
  Delete "$INSTDIR\Qt5Gui.dll"
  Delete "$INSTDIR\Qt5Widgets.dll"
  Delete "$INSTDIR\qt\imageformats\qgif.dll"
  Delete "$INSTDIR\qt\imageformats\qjpeg.dll"
  Delete "$INSTDIR\qt\styles\qwindowsvistastyle.dll"

  ;Rename before delete as these files may be in use
  !insertmacro RenameAndDelete "$INSTDIR\EqualizerAPO.dll"
  ${If} $PreviousApoPresent == "1"
    ${If} ${FileExists} "$INSTDIR\EqualizerAPO.dll"
      ; The active DLL could not be moved, so do not attempt to overwrite it.
      Call RollbackInstallTransaction
      DetailPrint "The existing EqualizerAPO.dll could not be moved aside."
      ${IfNot} ${Silent}
        MessageBox MB_ICONSTOP|MB_OK "The existing EqualizerAPO.dll is still in use and could not be replaced safely. Installation will stop."
      ${EndIf}
      Abort
    ${EndIf}
  ${EndIf}
  !insertmacro RenameAndDelete "$INSTDIR\libfftw3-3.dll"
  !insertmacro RenameAndDelete "$INSTDIR\libfftw3.dll"
  !insertmacro RenameAndDelete "$INSTDIR\fftw3.dll"
  !insertmacro RenameAndDelete "$INSTDIR\libsndfile-1.dll"
  !insertmacro RenameAndDelete "$INSTDIR\sndfile.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp100.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcr100.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp120.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcr120.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp140.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp140_1.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp140_2.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icudt.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icuin.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icuuc.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icudt78.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icuin78.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icuuc78.dll"
  !insertmacro RenameAndDelete "$INSTDIR\VoicemeeterClient.exe"
  !insertmacro RenameAndDelete "$INSTDIR\vcruntime140.dll"
  !insertmacro RenameAndDelete "$INSTDIR\vcruntime140_1.dll"
  !insertmacro RenameAndDelete "$INSTDIR\FLAC.dll"
  !insertmacro RenameAndDelete "$INSTDIR\libmp3lame.dll"
  !insertmacro RenameAndDelete "$INSTDIR\mpg123.dll"
  !insertmacro RenameAndDelete "$INSTDIR\ogg.dll"
  !insertmacro RenameAndDelete "$INSTDIR\opus.dll"
  !insertmacro RenameAndDelete "$INSTDIR\vorbis.dll"
  !insertmacro RenameAndDelete "$INSTDIR\vorbisenc.dll"
  !insertmacro RenameAndDelete "$INSTDIR\vorbisfile.dll"
  !insertmacro RenameAndDelete "$INSTDIR\d3dcompiler_47.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Core.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Gui.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Network.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Svg.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Widgets.dll"

  File "${BINPATH}\EqualizerAPO.dll"
  File "${BINPATH}\DeviceSelector.exe"
  File "${BINPATH}\Benchmark.exe"
  File "${BINPATH}\VoicemeeterClient.exe"
  File "${BINPATH}\UpdateChecker.exe"
  File /oname=NOTICE.md "..\NOTICE.md"
  File /oname=LICENSE.txt "..\LICENSE"
  File "${BINPATH_EDITOR}\Editor.exe"

  File "${LIBPATH}\libfftw3.dll"
  File "${LIBPATH}\fftw3.dll"
  File "${LIBPATH}\sndfile.dll"
  File "${LIBPATH}\FLAC.dll"
  File "${LIBPATH}\libmp3lame.dll"
  File "${LIBPATH}\mpg123.dll"
  File "${LIBPATH}\ogg.dll"
  File "${LIBPATH}\opus.dll"
  File "${LIBPATH}\vorbis.dll"
  File "${LIBPATH}\vorbisenc.dll"
  File "${LIBPATH}\vorbisfile.dll"
  File "${LIBPATH}\msvcp140.dll"
  File "${LIBPATH}\msvcp140_1.dll"
  File "${LIBPATH}\vcruntime140.dll"
  File "${LIBPATH}\vcruntime140_1.dll"
  File "${LIBPATH}\d3dcompiler_47.dll"
  File "${LIBPATH}\icuuc.dll"
  File "${LIBPATH}\Qt6Core.dll"
  File "${LIBPATH}\Qt6Gui.dll"
  File "${LIBPATH}\Qt6Network.dll"
  File "${LIBPATH}\Qt6Svg.dll"
  File "${LIBPATH}\Qt6Widgets.dll"

  CreateDirectory "$INSTDIR\qt"
  CreateDirectory "$INSTDIR\qt\generic"
  CreateDirectory "$INSTDIR\qt\iconengines"
  CreateDirectory "$INSTDIR\qt\imageformats"
  CreateDirectory "$INSTDIR\qt\networkinformation"
  CreateDirectory "$INSTDIR\qt\platforms"
  CreateDirectory "$INSTDIR\qt\styles"
  CreateDirectory "$INSTDIR\qt\tls"

  File /oname=qt\generic\qtuiotouchplugin.dll "${LIBPATH}\qt\generic\qtuiotouchplugin.dll"
  File /oname=qt\iconengines\qsvgicon.dll "${LIBPATH}\qt\iconengines\qsvgicon.dll"
  File /oname=qt\imageformats\qico.dll "${LIBPATH}\qt\imageformats\qico.dll"
  File /oname=qt\imageformats\qsvg.dll "${LIBPATH}\qt\imageformats\qsvg.dll"
  File /oname=qt\networkinformation\qnetworklistmanager.dll "${LIBPATH}\qt\networkinformation\qnetworklistmanager.dll"
  File /oname=qt\platforms\qwindows.dll "${LIBPATH}\qt\platforms\qwindows.dll"
  File /oname=qt\styles\qmodernwindowsstyle.dll "${LIBPATH}\qt\styles\qmodernwindowsstyle.dll"
  File /oname=qt\tls\qcertonlybackend.dll "${LIBPATH}\qt\tls\qcertonlybackend.dll"
  File /oname=qt\tls\qschannelbackend.dll "${LIBPATH}\qt\tls\qschannelbackend.dll"

  File "Configuration tutorial (online).url"
  File "Configuration reference (online).url"
  File "qt.conf"

  CreateDirectory "$INSTDIR\config"
  CreateDirectory "$INSTDIR\VSTPlugins"

  SetOverwrite off
  File /oname=config\config.txt "config\config.txt"
  File /oname=config\example.txt "config\example.txt"
  File /oname=config\demo.txt "config\demo.txt"
  File /oname=config\multichannel.txt "config\multichannel.txt"
  File /oname=config\iir_lowpass.txt "config\iir_lowpass.txt"
  File /oname=config\selective_delay.txt "config\selective_delay.txt"
  SetOverwrite on

  ; Do not make persistent system changes when the installer payload is incomplete.
  Call VerifyRequiredAssets

  Call BeginProtectedAudioOverride
  ${If} $ProtectedAudioOverrideActive != "1"
    StrCpy $FailedRegistrationCode "protected-audio recovery journal could not be created"
    Goto apoRegistrationFailed
  ${EndIf}
  ; Persist the possibility of partial COM registration before starting
  ; regsvr32. A power loss while the child process is running must still cause
  ; the next installer to unregister the new DLL before restoring the old one.
  StrCpy $NewApoRegistrationAttempted "1"
  ClearErrors
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "NewApoRegistrationAttempted" 1
  ${If} ${Errors}
    StrCpy $FailedRegistrationCode "registration recovery phase could not be persisted"
    Call RestoreProtectedAudioSetting
    Goto apoRegistrationFailed
  ${EndIf}
  ; RegDLL does not work for 64-bit DLLs. Treat process-creation failure as a
  ; hard failure; ExecWait's result variable is undefined in that case.
  ClearErrors
  StrCpy $1 "process did not start"
  ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\EqualizerAPO.dll"' $1
  ${If} ${Errors}
    StrCpy $FailedRegistrationCode "process did not start"
    Call RestoreProtectedAudioSetting
    Goto apoRegistrationFailed
  ${EndIf}
  Call RestoreProtectedAudioSetting
  ${If} $ProtectedAudioOverrideActive == "1"
    StrCpy $FailedRegistrationCode "protected-audio setting could not be restored"
    Goto apoRegistrationFailed
  ${EndIf}
  ${If} $1 != 0
    StrCpy $FailedRegistrationCode "$1"
    Goto apoRegistrationFailed
  ${EndIf}
  Goto apoRegistrationSucceeded

  apoRegistrationFailed:
  Call RollbackInstallTransaction
  ${IfNot} ${Silent}
    MessageBox MB_ICONSTOP|MB_OK "Equalizer APO could not be registered. Installation will stop before modifying audio devices.$\r$\n$\r$\nThis usually means a required runtime DLL is missing or incompatible.$\r$\n$\r$\nregsvr32 result: $FailedRegistrationCode$\r$\n$\r$\n$ApoRollbackStatus"
  ${EndIf}
  Abort

  apoRegistrationSucceeded:
  ; Grant write access to the config directory for all users.
  nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR\config" /grant *S-1-5-32-545:(OI)(CI)F /T /C'
  Pop $InstallOperationCode
  ${If} $InstallOperationCode == "error"
    StrCpy $InstallFailureReason "starting the config ACL update"
    Goto installTransactionFailed
  ${ElseIf} $InstallOperationCode != 0
    StrCpy $InstallFailureReason "updating the config ACL (exit code: $InstallOperationCode)"
    Goto installTransactionFailed
  ${EndIf}

  StrCpy $OLDINSTDIR ""
  ClearErrors
  ReadRegStr $OLDINSTDIR HKLM ${REGPATH} "InstallPath"
  !insertmacro WriteRequiredRegStr HKLM "${REGPATH}" "InstallPath" "$INSTDIR" "writing the installation path"

  ; Write ConfigPath if non-existing or if InstallPath has changed.
  StrCpy $0 ""
  ClearErrors
  ReadRegStr $0 HKLM ${REGPATH} "ConfigPath"
  ${If} $0 == ""
  ${OrIf} $INSTDIR != $OLDINSTDIR
    !insertmacro WriteRequiredRegStr HKLM "${REGPATH}" "ConfigPath" "$INSTDIR\config" "writing the configuration path"
  ${EndIf}

  StrCpy $0 ""
  ClearErrors
  ReadRegStr $0 HKLM ${REGPATH} "EnableTrace"
  ${If} $0 == ""
    !insertmacro WriteRequiredRegStr HKLM "${REGPATH}" "EnableTrace" "false" "writing the trace setting"
  ${EndIf}

  ClearErrors
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  ${If} ${Errors}
    StrCpy $InstallFailureReason "writing the uninstaller"
    Goto installTransactionFailed
  ${EndIf}

  ; Replace only the product shortcuts after registration succeeds. Unrelated
  ; shortcuts in the same custom folder remain untouched.
  !insertmacro DeleteProductShortcuts $OldStartMenuFolder
  ${If} $InstallOperationCode != 0
    StrCpy $InstallFailureReason "removing previous product shortcuts"
    Goto installTransactionFailed
  ${EndIf}
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
  ClearErrors
  CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
  ${If} ${Errors}
    StrCpy $InstallFailureReason "creating the Start Menu folder"
    Goto installTransactionFailed
  ${EndIf}
  !insertmacro CreateRequiredShortcut "$SMPROGRAMS\$StartMenuFolder\Equalizer APO Configuration Editor.lnk" "$INSTDIR\Editor.exe"
  !insertmacro CreateRequiredShortcut "$SMPROGRAMS\$StartMenuFolder\Configuration tutorial (online).lnk" "$INSTDIR\Configuration tutorial (online).url"
  !insertmacro CreateRequiredShortcut "$SMPROGRAMS\$StartMenuFolder\Configuration reference (online).lnk" "$INSTDIR\Configuration reference (online).url"
  !insertmacro CreateRequiredShortcut "$SMPROGRAMS\$StartMenuFolder\Equalizer APO Device Selector.lnk" "$INSTDIR\DeviceSelector.exe"
  !insertmacro CreateRequiredShortcut "$SMPROGRAMS\$StartMenuFolder\Benchmark.lnk" "$INSTDIR\Benchmark.exe"
  !insertmacro CreateRequiredShortcut "$SMPROGRAMS\$StartMenuFolder\Check for updates.lnk" "$INSTDIR\UpdateChecker.exe"
  !insertmacro CreateRequiredShortcut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  !insertmacro MUI_STARTMENU_WRITE_END
  !insertmacro WriteRequiredRegStr HKLM "${REGPATH}" "Start Menu Folder" "$StartMenuFolder" "writing the Start Menu folder"

  !insertmacro WriteRequiredRegStr HKLM "${UNINST_REGPATH}" "DisplayName" "${PRODUCT_FULL_LABEL}" "writing the uninstall display name"
  !insertmacro WriteRequiredRegStr HKLM "${UNINST_REGPATH}" "DisplayVersion" "${VERSION}" "writing the uninstall display version"
  !insertmacro WriteRequiredRegStr HKLM "${UNINST_REGPATH}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\"" "writing the uninstall command"
  !insertmacro WriteRequiredRegDWORD HKLM "${UNINST_REGPATH}" "NoModify" 1 "writing the uninstall NoModify flag"
  !insertmacro WriteRequiredRegDWORD HKLM "${UNINST_REGPATH}" "NoRepair" 1 "writing the uninstall NoRepair flag"

  ; Silent installation performs only the reversible service restart inside the
  ; transaction. Interactive endpoint selection is intentionally post-commit.
  ${If} ${Silent}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "DeviceSelectorMode" 1
    ${If} ${Errors}
      StrCpy $InstallFailureReason "persisting the silent Device Selector recovery mode"
      Goto installTransactionFailed
    ${EndIf}
    ClearErrors
    WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "DeviceSelectorOperationStarted" 1
    ${If} ${Errors}
      StrCpy $InstallFailureReason "persisting the silent Device Selector recovery phase"
      Goto installTransactionFailed
    ${EndIf}
    ClearErrors
    StrCpy $DeviceSelectorResult "process did not start"
    ExecWait '"$INSTDIR\DeviceSelector.exe" /r /s' $DeviceSelectorResult
    ${If} ${Errors}
      SetRebootFlag true
      StrCpy $InstallFailureReason "starting the silent audio-service restart"
      Goto installTransactionFailed
    ${ElseIf} $DeviceSelectorResult != 0
      SetRebootFlag true
      StrCpy $InstallFailureReason "restarting the audio service (exit code: $DeviceSelectorResult)"
      Goto installTransactionFailed
    ${EndIf}
  ${EndIf}

  ClearErrors
  WriteRegDWORD HKLM ${INSTALLER_APP_RECOVERY_REGPATH} "UpdaterOperationStarted" 1
  ${If} ${Errors}
    StrCpy $InstallFailureReason "persisting the updater recovery phase"
    Goto installTransactionFailed
  ${EndIf}
  ClearErrors
  StrCpy $InstallOperationCode "process did not start"
  ${If} ${SectionIsSelected} ${SecCheckForUpdates}
    ${If} ${Silent}
      ExecWait '"$INSTDIR\UpdateChecker.exe" -i -s' $InstallOperationCode
    ${Else}
      ExecWait '"$INSTDIR\UpdateChecker.exe" -i' $InstallOperationCode
    ${EndIf}
  ${Else}
    ${If} ${Silent}
      ExecWait '"$INSTDIR\UpdateChecker.exe" -u -s' $InstallOperationCode
    ${Else}
      ExecWait '"$INSTDIR\UpdateChecker.exe" -u' $InstallOperationCode
    ${EndIf}
  ${EndIf}
  ${If} ${Errors}
    StrCpy $InstallFailureReason "starting Update Checker task configuration"
    Goto installTransactionFailed
  ${ElseIf} $InstallOperationCode != 0
    StrCpy $InstallFailureReason "configuring the Update Checker task (exit code: $InstallOperationCode)"
    Goto installTransactionFailed
  ${EndIf}

  ; The durable commit ends the rollback transaction. Endpoint selection below
  ; is a separate user action and can never cause transaction rollback.
  Call CommitInstallTransaction
  ${If} $InstallRecoveryFailed == "1"
    StrCpy $InstallFailureReason "writing the final installation commit marker"
    Goto installTransactionFailed
  ${EndIf}
  ${IfNot} ${Silent}
    ClearErrors
    StrCpy $DeviceSelectorResult "process did not start"
    ExecWait '"$INSTDIR\DeviceSelector.exe" /i' $DeviceSelectorResult
    ${If} ${Errors}
      SetRebootFlag true
      MessageBox MB_ICONEXCLAMATION|MB_OK "Installation completed, but Device Selector could not be started. Run Equalizer APO Device Selector from the Start Menu to choose audio endpoints. No committed files were rolled back."
    ${ElseIf} $DeviceSelectorResult != 0
      SetRebootFlag true
      MessageBox MB_ICONEXCLAMATION|MB_OK "Installation completed, but Device Selector exited with code $DeviceSelectorResult. Run it again from the Start Menu if endpoint changes were not applied. No committed files were rolled back."
    ${EndIf}
  ${EndIf}
  Goto installTransactionComplete

  installTransactionFailed:
  Call RollbackInstallTransaction
  ${IfNot} ${Silent}
    MessageBox MB_ICONSTOP|MB_OK "Installation could not complete while $InstallFailureReason.$\r$\n$\r$\nThe installer attempted to restore the previous application files, registry metadata, shortcuts, update task, audio registration and protected-audio setting.$\r$\n$\r$\n$ApoRollbackStatus"
  ${EndIf}
  Abort

  installTransactionComplete:
SectionEnd

;--------------------------------
;Uninstaller Sections

Function un.ValidateProductChildDirectory
  ; Validate a caller-supplied product child without following a directory
  ; reparse point. Missing children are safe no-ops; access errors fail closed.
  StrCpy $InstallRecoveryFailed "0"
  StrCpy $InstallRecoveryPathExists "0"
  ClearErrors
  GetFullPathName $0 "$INSTDIR"
  ${If} ${Errors}
  ${OrIf} $0 == ""
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  normalizeUninstallRootTail:
  StrCpy $2 "$0" 1 -1
  ${If} $2 == "\"
    ${GetRoot} "$0" $3
    ${If} $0 != $3
    ${AndIf} $0 != "$3\"
      StrCpy $0 "$0" -1
      Goto normalizeUninstallRootTail
    ${EndIf}
  ${EndIf}

  ClearErrors
  GetFullPathName $1 "$InstallRecoveryPathToCheck"
  ${If} ${Errors}
  ${OrIf} $1 == ""
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  StrCpy $2 "$0\"
  StrLen $3 "$2"
  StrCpy $4 "$1" $3
  ${If} $4 != "$2"
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  System::Call 'kernel32::GetFileAttributesW(w "$0") i .r2 ?e'
  Pop $3
  ${If} $2 == ${INVALID_FILE_ATTRIBUTES}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  IntOp $3 $2 & ${FILE_ATTRIBUTE_DIRECTORY}
  ${If} $3 == 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  IntOp $3 $2 & ${FILE_ATTRIBUTE_REPARSE_POINT}
  ${If} $3 != 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}

  System::Call 'kernel32::GetFileAttributesW(w "$1") i .r2 ?e'
  Pop $3
  ${If} $2 == ${INVALID_FILE_ATTRIBUTES}
    ${If} $3 == ${ERROR_FILE_NOT_FOUND}
    ${OrIf} $3 == ${ERROR_PATH_NOT_FOUND}
      StrCpy $InstallRecoveryPathToCheck "$1"
      Return
    ${EndIf}
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  IntOp $3 $2 & ${FILE_ATTRIBUTE_DIRECTORY}
  ${If} $3 == 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  IntOp $3 $2 & ${FILE_ATTRIBUTE_REPARSE_POINT}
  ${If} $3 != 0
    StrCpy $InstallRecoveryFailed "1"
    Return
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$1"
  StrCpy $InstallRecoveryPathExists "1"
FunctionEnd

Function un.RemoveQtPluginTreeSafely
  ; Validate every product-owned directory which will be traversed by an exact
  ; Delete path. Unknown content and every reparse point are retained.
  StrCpy $InstallRecoveryPathToCheck "$INSTDIR\qt"
  Call un.ValidateProductChildDirectory
  ${If} $InstallRecoveryFailed == "1"
    Goto unsafeQtTree
  ${EndIf}
  ${If} $InstallRecoveryPathExists == "0"
    Return
  ${EndIf}

  StrCpy $InstallRecoveryPathToCheck "$INSTDIR\qt\generic"
  Call un.ValidateProductChildDirectory
  ${If} $InstallRecoveryFailed == "1"
    Goto unsafeQtTree
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$INSTDIR\qt\iconengines"
  Call un.ValidateProductChildDirectory
  ${If} $InstallRecoveryFailed == "1"
    Goto unsafeQtTree
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$INSTDIR\qt\imageformats"
  Call un.ValidateProductChildDirectory
  ${If} $InstallRecoveryFailed == "1"
    Goto unsafeQtTree
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$INSTDIR\qt\networkinformation"
  Call un.ValidateProductChildDirectory
  ${If} $InstallRecoveryFailed == "1"
    Goto unsafeQtTree
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$INSTDIR\qt\platforms"
  Call un.ValidateProductChildDirectory
  ${If} $InstallRecoveryFailed == "1"
    Goto unsafeQtTree
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$INSTDIR\qt\styles"
  Call un.ValidateProductChildDirectory
  ${If} $InstallRecoveryFailed == "1"
    Goto unsafeQtTree
  ${EndIf}
  StrCpy $InstallRecoveryPathToCheck "$INSTDIR\qt\tls"
  Call un.ValidateProductChildDirectory
  ${If} $InstallRecoveryFailed == "1"
    Goto unsafeQtTree
  ${EndIf}

  Delete "$INSTDIR\qt\generic\qtuiotouchplugin.dll"
  Delete "$INSTDIR\qt\iconengines\qsvgicon.dll"
  Delete "$INSTDIR\qt\imageformats\qico.dll"
  Delete "$INSTDIR\qt\imageformats\qsvg.dll"
  Delete "$INSTDIR\qt\imageformats\qgif.dll"
  Delete "$INSTDIR\qt\imageformats\qjpeg.dll"
  Delete "$INSTDIR\qt\networkinformation\qnetworklistmanager.dll"
  Delete "$INSTDIR\qt\platforms\qwindows.dll"
  Delete "$INSTDIR\qt\styles\qmodernwindowsstyle.dll"
  Delete "$INSTDIR\qt\styles\qwindowsvistastyle.dll"
  Delete "$INSTDIR\qt\tls\qcertonlybackend.dll"
  Delete "$INSTDIR\qt\tls\qschannelbackend.dll"
  RMDir "$INSTDIR\qt\generic"
  RMDir "$INSTDIR\qt\iconengines"
  RMDir "$INSTDIR\qt\imageformats"
  RMDir "$INSTDIR\qt\networkinformation"
  RMDir "$INSTDIR\qt\platforms"
  RMDir "$INSTDIR\qt\styles"
  RMDir "$INSTDIR\qt\tls"
  RMDir "$INSTDIR\qt"
  Return

  unsafeQtTree:
  DetailPrint "The Qt plug-in tree contains an unsafe or inaccessible path and was retained: $INSTDIR\qt"
FunctionEnd

LangString SecRemoveName ${LANG_ENGLISH} "Remove configurations and registry backups"
LangString SecRemoveName ${LANG_SPANISH} "Eliminar configuraciones y copias de seguridad del registro"
LangString SecRemoveName ${LANG_GERMAN} "Konfigurationen und Registrierungsbackups entfernen"
LangString SecRemoveName ${LANG_TRADCHINESE} "移除設定檔與登錄檔備份"
LangString SecRemoveName ${LANG_SIMPCHINESE} "移除配置文件与注册表备份"

Section /o un.$(SecRemoveName)

  Delete "$INSTDIR\*.reg"
  StrCpy $InstallRecoveryPathToCheck "$INSTDIR\config"
  Call un.ValidateProductChildDirectory
  ${If} $InstallRecoveryFailed == "1"
    DetailPrint "The configuration directory contains an unsafe or inaccessible path and was retained: $INSTDIR\config"
  ${ElseIf} $InstallRecoveryPathExists == "1"
    ; Delete immediate configuration files only. Subdirectories are retained so
    ; the elevated uninstaller never recursively follows a user-created junction.
    Delete "$InstallRecoveryPathToCheck\*.*"
    RMDir /REBOOTOK "$InstallRecoveryPathToCheck"
    ${If} ${FileExists} "$InstallRecoveryPathToCheck"
      DetailPrint "Configuration subdirectories or locked files were retained: $InstallRecoveryPathToCheck"
    ${EndIf}
  ${EndIf}
  DeleteRegKey HKCU ${REGPATH}

SectionEnd

Section "-un.Uninstall"
  !if ${LIBPATH} != "lib32"
	SetRegView 64
  !endif

  ;Qt applications only work if working directory is set to application directory
  Push $OUTDIR
  SetOutPath $INSTDIR
  ${If} ${Silent}
    ExecWait '"$INSTDIR\UpdateChecker.exe" -u -s'
    ExecWait '"$INSTDIR\DeviceSelector.exe" /u /s'
  ${Else}
    ExecWait '"$INSTDIR\UpdateChecker.exe" -u'
    ExecWait '"$INSTDIR\DeviceSelector.exe" /u'
  ${EndIf}
  Pop $OUTDIR
  SetOutPath $OUTDIR

  ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\EqualizerAPO.dll"'

  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
  !insertmacro DeleteProductShortcuts $StartMenuFolder

  RMDir "$INSTDIR\VSTPlugins"

  Delete "$INSTDIR\Configuration reference (online).url"
  Delete "$INSTDIR\Configuration tutorial (online).url"

  Call un.RemoveQtPluginTreeSafely

  Delete "$INSTDIR\Qt6Widgets.dll"
  Delete "$INSTDIR\Qt6Svg.dll"
  Delete "$INSTDIR\Qt6Network.dll"
  Delete "$INSTDIR\Qt6Gui.dll"
  Delete "$INSTDIR\Qt6Core.dll"
  Delete "$INSTDIR\icuuc.dll"
  Delete "$INSTDIR\dxil.dll"
  Delete "$INSTDIR\dxcompiler.dll"
  Delete "$INSTDIR\d3dcompiler_47.dll"
  Delete "$INSTDIR\vcruntime140_1.dll"
  Delete "$INSTDIR\vcruntime140.dll"
  Delete "$INSTDIR\msvcp140_1.dll"
  Delete "$INSTDIR\msvcp140.dll"
  Delete /REBOOTOK "$INSTDIR\sndfile.dll"
  Delete /REBOOTOK "$INSTDIR\libfftw3.dll"
  Delete /REBOOTOK "$INSTDIR\fftw3.dll"
  Delete "$INSTDIR\Editor.exe"
  Delete "$INSTDIR\qt.conf"
  Delete "$INSTDIR\NOTICE.md"
  Delete "$INSTDIR\LICENSE.txt"

  Delete /REBOOTOK "$INSTDIR\FLAC.dll"
  Delete /REBOOTOK "$INSTDIR\libmp3lame.dll"
  Delete /REBOOTOK "$INSTDIR\mpg123.dll"
  Delete /REBOOTOK "$INSTDIR\ogg.dll"
  Delete /REBOOTOK "$INSTDIR\opus.dll"
  Delete /REBOOTOK "$INSTDIR\vorbis.dll"
  Delete /REBOOTOK "$INSTDIR\vorbisenc.dll"
  Delete /REBOOTOK "$INSTDIR\vorbisfile.dll"

  Delete "$INSTDIR\UpdateChecker.exe"
  Delete "$INSTDIR\VoicemeeterClient.exe"
  Delete "$INSTDIR\Benchmark.exe"
  Delete "$INSTDIR\DeviceSelector.exe"
  Delete /REBOOTOK "$INSTDIR\EqualizerAPO.dll"

  Delete "$INSTDIR\Uninstall.exe"

  ;Only remove if empty
  RMDir /REBOOTOK "$INSTDIR"

  DeleteRegKey HKLM ${UNINST_REGPATH}
  DeleteRegKey /ifempty HKLM ${REGPATH}

SectionEnd
