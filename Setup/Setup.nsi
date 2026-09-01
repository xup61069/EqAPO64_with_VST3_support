!include "LogicLib.nsh"
!include "MUI2.nsh"
!include "StrFunc.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

${StrTrimNewLines}

Unicode true
ManifestDPIAware true
CRCCheck force
;Use more efficient compression
SetCompressor /SOLID lzma

!searchparse /file ..\version.h `#define MAJOR ` MAJOR
!searchparse /file ..\version.h `#define MINOR ` MINOR
!searchparse /file ..\version.h `#define REVISION ` REVISION
!define VERSION ${MAJOR}.${MINOR}.${REVISION}

!define REGPATH "Software\EqualizerAPO"
!define UNINST_REGPATH "Software\Microsoft\Windows\CurrentVersion\Uninstall\EqualizerAPO"
!define INSTALLER_RECOVERY_REGPATH "${REGPATH}\InstallerRecovery"

VIProductVersion "${MAJOR}.${MINOR}.${REVISION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "Equalizer APO"
VIAddVersionKey /LANG=1033 "FileDescription" "Equalizer APO x64 Installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Equalizer APO contributors"

;--------------------------------
;General

  ;Name and file
  Name "Equalizer APO ${VERSION}"

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
  Var InstallRollbackFiles
  Var InstallRollbackState
  Var InstallRollbackCopyCode
  Var PreviousApoPresent
  Var NewApoRegistrationAttempted
  Var RenameManifestPath
  Var RenameManifestHandle
  Var ApoRollbackRegistrationCode
  Var ApoRollbackUnregistrationCode
  Var ApoRollbackStatus
  Var FailedRegistrationCode
  
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
    ClearErrors
    Rename "${path}" "$renamePath"
    ${IfNot} ${Errors}
      ClearErrors
      FileWrite $RenameManifestHandle "$renamePath$\r$\n"
      ${If} ${Errors}
        DetailPrint "Could not record a renamed application file for rollback: $renamePath"
        Call RollbackInstallTransaction
        Abort
      ${EndIf}
    ${EndIf}
  ${EndIf}
!macroend

!macro DeleteProductShortcuts folder
  ${If} "${folder}" != ""
    Delete "$SMPROGRAMS\${folder}\Equalizer APO Configuration Editor.lnk"
    Delete "$SMPROGRAMS\${folder}\Configuration tutorial (online).lnk"
    Delete "$SMPROGRAMS\${folder}\Configuration reference (online).lnk"
    Delete "$SMPROGRAMS\${folder}\Equalizer APO Device Selector.lnk"
    Delete "$SMPROGRAMS\${folder}\Benchmark.lnk"
    Delete "$SMPROGRAMS\${folder}\Check for updates.lnk"
    Delete "$SMPROGRAMS\${folder}\Uninstall.lnk"
    ; Keep unrelated user shortcuts and remove the folder only when empty.
    RMDir "$SMPROGRAMS\${folder}"
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

Function RemoveInstalledProductFiles
  ; Current payload. Keep config and VSTPlugins intact even on a fresh-install
  ; failure: an existing folder may already contain user-created data.
  !insertmacro DeleteTransactionFile "$INSTDIR\EqualizerAPO.dll"
  !insertmacro DeleteTransactionFile "$INSTDIR\DeviceSelector.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\Benchmark.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\VoicemeeterClient.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\UpdateChecker.exe"
  !insertmacro DeleteTransactionFile "$INSTDIR\Editor.exe"
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

  ; Delete only exact paths recorded after successful Rename calls. This avoids
  ; treating arbitrary pre-existing *.old files as transaction-owned.
  ClearErrors
  FileOpen $0 "$RenameManifestPath" r
  ${If} ${Errors}
    Goto renameManifestCleanup
  ${EndIf}

  readRenamedProductFile:
  ClearErrors
  FileRead $0 $1
  ${If} ${Errors}
    Goto closeRenameManifest
  ${EndIf}
  ${StrTrimNewLines} $1 "$1"
  ${If} $1 != ""
    Delete /REBOOTOK "$1"
  ${EndIf}
  Goto readRenamedProductFile

  closeRenameManifest:
  FileClose $0
  renameManifestCleanup:
  Delete "$RenameManifestPath"
  RMDir /r "$InstallRollbackDirectory"
FunctionEnd

Function PrepareInstallTransaction
  StrCpy $InstallRollbackState "0"
  StrCpy $PreviousApoPresent "0"
  StrCpy $NewApoRegistrationAttempted "0"
  StrCpy $RenameManifestHandle ""
  StrCpy $InstallRollbackDirectory ""
  ${If} ${FileExists} "$INSTDIR\EqualizerAPO.dll"
    StrCpy $PreviousApoPresent "1"
  ${EndIf}

  ; Use a unique durable temporary directory rather than $PLUGINSDIR. If an
  ; automatic restore cannot finish, the only recovery copy must survive setup
  ; process cleanup for manual recovery.
  ClearErrors
  GetTempFileName $InstallRollbackDirectory
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "temporary path creation failed"
    Goto installBackupFailed
  ${EndIf}
  Delete "$InstallRollbackDirectory"
  CreateDirectory "$InstallRollbackDirectory"
  StrCpy $InstallRollbackFiles "$InstallRollbackDirectory\files"
  StrCpy $RenameManifestPath "$InstallRollbackDirectory\renamed-files.txt"
  CreateDirectory "$InstallRollbackFiles"

  ; Snapshot the old application tree outside $INSTDIR before changing files.
  ; config and VSTPlugins are excluded because they are user-data containers and
  ; are never deleted or overwritten by rollback.
  nsExec::ExecToLog '"$SYSDIR\robocopy.exe" "$INSTDIR" "$InstallRollbackFiles" /E /COPY:DATS /DCOPY:DAT /R:1 /W:1 /XJ /XD "$INSTDIR\config" "$INSTDIR\VSTPlugins" /NFL /NDL /NJH /NJS /NP'
  Pop $InstallRollbackCopyCode
  ${If} $InstallRollbackCopyCode == "error"
    Goto installBackupFailed
  ${ElseIf} $InstallRollbackCopyCode >= 8
    Goto installBackupFailed
  ${EndIf}

  ClearErrors
  FileOpen $RenameManifestHandle "$RenameManifestPath" w
  ${If} ${Errors}
    StrCpy $InstallRollbackCopyCode "rename manifest creation failed"
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
  ${If} $InstallRollbackDirectory != ""
    RMDir /r "$InstallRollbackDirectory"
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

  DetailPrint "Registration did not complete; restoring the pre-install application files."
  StrCpy $ApoRollbackRegistrationCode "not attempted"
  StrCpy $ApoRollbackStatus "The new application files were removed. User configuration was preserved."
  Call CloseRenameManifest

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
  ${EndIf}

  Call RemoveInstalledProductFiles

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
  ${Else}
    StrCpy $ApoRollbackStatus "The new application files were removed. User configuration was preserved."
    RMDir "$INSTDIR"
  ${EndIf}

  DetailPrint "$ApoRollbackStatus"
  Call DiscardRenamedProductFiles
  StrCpy $InstallRollbackState "0"
FunctionEnd

Function CommitInstallTransaction
  ; Registration is the commit point. Registry metadata, shortcuts, ACLs and
  ; audio-device changes are intentionally performed only after this call. Keep
  ; the rename manifest until the audio service releases the old files.
  Call CloseRenameManifest
  StrCpy $NewApoRegistrationAttempted "0"
  StrCpy $InstallRollbackState "0"
  DetailPrint "Application-file transaction committed."
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
  ; Snapshot every installed application file before deleting or replacing it.
  Call PrepareInstallTransaction

  ; Read the previous shortcut location now, but do not remove it until the new
  ; APO has registered successfully.
  !insertmacro MUI_STARTMENU_GETFOLDER Application $OldStartMenuFolder

  ;Migrate the versioned default folder while preserving custom folder names.
  StrCpy $0 "$OldStartMenuFolder" 14
  ${If} $0 == "Equalizer APO "
    StrCpy $StartMenuFolder "Equalizer APO ${VERSION}"
  ${ElseIf} $StartMenuFolder == ""
    StrCpy $StartMenuFolder "Equalizer APO ${VERSION}"
  ${EndIf}
  
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
  StrCpy $NewApoRegistrationAttempted "1"
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
  ; Registration is the commit point. Only now may setup change persistent
  ; metadata, shortcuts, permissions or audio-device registrations.
  Call CommitInstallTransaction

  ; Grant write access to the config directory for all users.
  nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR\config" /grant *S-1-5-32-545:(OI)(CI)F /T /C'

  ReadRegStr $OLDINSTDIR HKLM ${REGPATH} "InstallPath"
  WriteRegStr HKLM ${REGPATH} "InstallPath" "$INSTDIR"

  ; Write ConfigPath if non-existing or if InstallPath has changed.
  ReadRegStr $0 HKLM ${REGPATH} "ConfigPath"
  ${If} $0 == ""
  ${OrIf} $INSTDIR != $OLDINSTDIR
    WriteRegStr HKLM ${REGPATH} "ConfigPath" "$INSTDIR\config"
  ${EndIf}

  ReadRegStr $0 HKLM ${REGPATH} "EnableTrace"
  ${If} $0 == ""
    WriteRegStr HKLM ${REGPATH} "EnableTrace" "false"
  ${EndIf}

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Replace only the product shortcuts after registration succeeds. Unrelated
  ; shortcuts in the same custom folder remain untouched.
  !insertmacro DeleteProductShortcuts $OldStartMenuFolder
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
  CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Equalizer APO Configuration Editor.lnk" "$INSTDIR\Editor.exe"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Configuration tutorial (online).lnk" "$INSTDIR\Configuration tutorial (online).url"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Configuration reference (online).lnk" "$INSTDIR\Configuration reference (online).url"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Equalizer APO Device Selector.lnk" "$INSTDIR\DeviceSelector.exe"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Benchmark.lnk" "$INSTDIR\Benchmark.exe"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Check for updates.lnk" "$INSTDIR\UpdateChecker.exe"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  !insertmacro MUI_STARTMENU_WRITE_END

  WriteRegStr HKLM ${UNINST_REGPATH} "DisplayName" "Equalizer APO"
  WriteRegStr HKLM ${UNINST_REGPATH} "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM ${UNINST_REGPATH} "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM ${UNINST_REGPATH} "NoModify" 1
  WriteRegDWORD HKLM ${UNINST_REGPATH} "NoRepair" 1

  ${If} ${Silent}
    ; Preserve existing endpoint registrations and restart AudioSrv without opening dialogs.
    ExecWait '"$INSTDIR\DeviceSelector.exe" /r /s' $0
  ${Else}
    ExecWait '"$INSTDIR\DeviceSelector.exe" /i' $0
  ${EndIf}
  
  ${If} ${SectionIsSelected} ${SecCheckForUpdates}
    ${If} ${Silent}
      ExecWait '"$INSTDIR\UpdateChecker.exe" -i -s'
    ${Else}
      ExecWait '"$INSTDIR\UpdateChecker.exe" -i'
    ${EndIf}
  ${Else}
    ${If} ${Silent}
      ExecWait '"$INSTDIR\UpdateChecker.exe" -u -s'
    ${Else}
      ExecWait '"$INSTDIR\UpdateChecker.exe" -u'
    ${EndIf}
  ${EndIf}

  ; The audio service has restarted; discard only installer-owned rename
  ; artifacts, never every *.old file in the installation directory.
  Call DiscardRenamedProductFiles
  
  ${If} $0 == "0"
    SetRebootFlag false
  ${Else}
    SetRebootFlag true
  ${EndIf}
  
SectionEnd

;--------------------------------
;Uninstaller Sections

LangString SecRemoveName ${LANG_ENGLISH} "Remove configurations and registry backups"
LangString SecRemoveName ${LANG_SPANISH} "Eliminar configuraciones y copias de seguridad del registro"
LangString SecRemoveName ${LANG_GERMAN} "Konfigurationen und Registrierungsbackups entfernen"
LangString SecRemoveName ${LANG_TRADCHINESE} "移除設定檔與登錄檔備份"
LangString SecRemoveName ${LANG_SIMPCHINESE} "移除配置文件与注册表备份"

Section /o un.$(SecRemoveName)
  
  Delete "$INSTDIR\*.reg"
  RMDir /REBOOTOK /r "$INSTDIR\config"
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
  
  RMDir /r "$INSTDIR\qt"
  
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
