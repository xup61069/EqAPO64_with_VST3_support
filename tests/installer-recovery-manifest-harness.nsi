Unicode true
SilentInstall silent
RequestExecutionLevel user

!ifndef HARNESS_OUTPUT
  !error "HARNESS_OUTPUT must point to the harness executable"
!endif

OutFile "${HARNESS_OUTPUT}"

!include "LogicLib.nsh"
!include "..\Setup\InstallerRecoveryManifest.nsh"

Var ManifestPath
Var ManifestHandle
Var ManifestWriteFailed
Var RenameManifestHandle
Var RenameCleanupInstallPrefix
Var ReadLine

; The production identity helper uses these scratch registers. The cleanup
; reader must survive the same clobbering between consecutive manifest records.
Function ClobberIdentityScratchRegisters
  StrCpy $0 "clobbered-0"
  StrCpy $1 "clobbered-1"
  StrCpy $2 "clobbered-2"
  StrCpy $3 "clobbered-3"
  StrCpy $4 "clobbered-4"
  StrCpy $5 "clobbered-5"
  StrCpy $6 "clobbered-6"
  StrCpy $7 "clobbered-7"
  StrCpy $8 "clobbered-8"
FunctionEnd

!macro AppendHarnessLine line failureCode
  !insertmacro AppendInstallerRecoveryManifestLine \
    $ManifestHandle "$ManifestPath" "${line}" $ManifestWriteFailed
  ${If} $ManifestWriteFailed == "1"
    SetErrorLevel ${failureCode}
    Goto harnessDone
  ${EndIf}
!macroend

!macro ReadHarnessLine expected failureCode
  ClearErrors
  FileReadUTF16LE $RenameManifestHandle $ReadLine
  ${If} ${Errors}
  ${OrIf} $ReadLine != "${expected}$\r$\n"
    SetErrorLevel ${failureCode}
    Goto harnessDone
  ${EndIf}
!macroend

Section
  StrCpy $ManifestHandle ""
  StrCpy $RenameManifestHandle ""
  InitPluginsDir
  StrCpy $ManifestPath "$PLUGINSDIR\renamed-files.txt"
  StrCpy $RenameCleanupInstallPrefix "$PLUGINSDIR\"

  !insertmacro AppendHarnessLine "first" 11
  !insertmacro AppendHarnessLine "second-line-is-deliberately-longer" 12
  !insertmacro AppendHarnessLine "路徑-🔊-z" 13

  ClearErrors
  FileOpen $RenameManifestHandle "$ManifestPath" r
  ${If} ${Errors}
    SetErrorLevel 20
    Goto harnessDone
  ${EndIf}
  !insertmacro ReadHarnessLine "first" 21
  Call ClobberIdentityScratchRegisters
  ${If} $RenameCleanupInstallPrefix != "$PLUGINSDIR\"
    SetErrorLevel 25
    Goto harnessDone
  ${EndIf}
  !insertmacro ReadHarnessLine "second-line-is-deliberately-longer" 22
  !insertmacro ReadHarnessLine "路徑-🔊-z" 23

  ClearErrors
  FileReadUTF16LE $RenameManifestHandle $ReadLine
  ${IfNot} ${Errors}
    SetErrorLevel 24
    Goto harnessDone
  ${EndIf}
  FileClose $RenameManifestHandle
  StrCpy $RenameManifestHandle ""
  ClearErrors
  Delete "$ManifestPath"
  ${If} ${Errors}
    SetErrorLevel 26
    Goto harnessDone
  ${EndIf}
  SetErrorLevel 0

  harnessDone:
  ${If} $RenameManifestHandle != ""
    FileClose $RenameManifestHandle
  ${EndIf}
  ${If} $ManifestHandle != ""
    FileClose $ManifestHandle
  ${EndIf}
SectionEnd
