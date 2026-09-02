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
Var ReadHandle
Var ReadLine

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
  FileReadUTF16LE $ReadHandle $ReadLine
  ${If} ${Errors}
  ${OrIf} $ReadLine != "${expected}$\r$\n"
    SetErrorLevel ${failureCode}
    Goto harnessDone
  ${EndIf}
!macroend

Section
  StrCpy $ManifestHandle ""
  StrCpy $ReadHandle ""
  InitPluginsDir
  StrCpy $ManifestPath "$PLUGINSDIR\renamed-files.txt"

  !insertmacro AppendHarnessLine "first" 11
  !insertmacro AppendHarnessLine "second-line-is-deliberately-longer" 12
  !insertmacro AppendHarnessLine "路徑-🔊-z" 13

  ClearErrors
  FileOpen $ReadHandle "$ManifestPath" r
  ${If} ${Errors}
    SetErrorLevel 20
    Goto harnessDone
  ${EndIf}
  !insertmacro ReadHarnessLine "first" 21
  !insertmacro ReadHarnessLine "second-line-is-deliberately-longer" 22
  !insertmacro ReadHarnessLine "路徑-🔊-z" 23

  ClearErrors
  FileReadUTF16LE $ReadHandle $ReadLine
  ${IfNot} ${Errors}
    SetErrorLevel 24
    Goto harnessDone
  ${EndIf}
  SetErrorLevel 0

  harnessDone:
  ${If} $ReadHandle != ""
    FileClose $ReadHandle
  ${EndIf}
  ${If} $ManifestHandle != ""
    FileClose $ManifestHandle
  ${EndIf}
SectionEnd
