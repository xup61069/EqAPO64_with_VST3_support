param(
	[string]$Makensis = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if ($Makensis -eq "") {
	$localMakensis = Join-Path $root "third_party\nsis-3.11\makensis.exe"
	if (Test-Path -LiteralPath $localMakensis) {
		$Makensis = $localMakensis
	}
}

if ($Makensis -eq "") {
	$makensisCommand = Get-Command makensis.exe -ErrorAction SilentlyContinue
	if ($null -ne $makensisCommand) {
		$Makensis = $makensisCommand.Source
	}
}

if ($Makensis -eq "") {
	$candidates = @(
		Join-Path ${env:ProgramFiles} "NSIS\makensis.exe",
		Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"
	)
	$found = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
	if ($null -ne $found) {
		$Makensis = $found
	}
}

if ($Makensis -eq "" -or !(Test-Path -LiteralPath $Makensis)) {
	throw "makensis.exe was not found. Bootstrap NSIS or pass -Makensis <path>."
}

$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) `
	("EqualizerAPO-manifest-harness-" + [Guid]::NewGuid().ToString("N"))
$harnessExe = Join-Path $testDirectory "installer-recovery-manifest-harness.exe"
$harnessSource = Join-Path $root "tests\installer-recovery-manifest-harness.nsi"

New-Item -ItemType Directory -Path $testDirectory | Out-Null
try {
	& $Makensis "/WX" "/INPUTCHARSET" "UTF8" "/DHARNESS_OUTPUT=$harnessExe" $harnessSource
	if ($LASTEXITCODE -ne 0) {
		throw "Installer recovery manifest harness compilation failed with exit code $LASTEXITCODE"
	}

	$harnessProcess = Start-Process -FilePath $harnessExe -Wait -PassThru -WindowStyle Hidden
	if ($harnessProcess.ExitCode -ne 0) {
		throw "Installer recovery manifest harness failed with exit code $($harnessProcess.ExitCode)"
	}
}
finally {
	if (Test-Path -LiteralPath $harnessExe) {
		Remove-Item -LiteralPath $harnessExe -Force
	}
	if (Test-Path -LiteralPath $testDirectory) {
		Remove-Item -LiteralPath $testDirectory -Force
	}
}

Write-Host "Installer recovery manifest append harness passed."
