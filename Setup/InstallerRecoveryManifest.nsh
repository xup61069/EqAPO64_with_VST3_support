!ifndef INSTALLER_RECOVERY_MANIFEST_NSH
!define INSTALLER_RECOVERY_MANIFEST_NSH

; NSIS FileOpen mode "a" preserves an existing file but leaves the file pointer
; at offset zero. Always seek explicitly before appending a rename confirmation.
; The result variable is independent of the NSIS error flag so FileClose cannot
; hide an earlier seek or write failure.
!macro AppendInstallerRecoveryManifestLine handle path line result
  StrCpy ${result} "0"
  ClearErrors
  FileOpen ${handle} "${path}" a
  ${If} ${Errors}
    StrCpy ${result} "1"
  ${Else}
    ClearErrors
    FileSeek ${handle} 0 END
    ${If} ${Errors}
      StrCpy ${result} "1"
    ${Else}
      ClearErrors
      FileWriteUTF16LE ${handle} "${line}$\r$\n"
      ${If} ${Errors}
        StrCpy ${result} "1"
      ${EndIf}
    ${EndIf}
    FileClose ${handle}
  ${EndIf}
  StrCpy ${handle} ""
!macroend

!endif
