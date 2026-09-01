param(
	[string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$benchmark = Join-Path $root "Benchmark\x64\$Configuration\Benchmark.exe"
$runtimeDirectory = Join-Path $root "Setup\lib64"

if (!(Test-Path -LiteralPath $benchmark)) {
	throw "Benchmark executable not found: $benchmark"
}
if (!(Test-Path -LiteralPath $runtimeDirectory)) {
	throw "Staged runtime directory not found: $runtimeDirectory"
}

$systemTemp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$testDirectory = Join-Path $systemTemp ("eqapo-loudness-test-" + [guid]::NewGuid().ToString("N"))
$resolvedTestDirectory = [System.IO.Path]::GetFullPath($testDirectory)
if (!$resolvedTestDirectory.StartsWith($systemTemp, [System.StringComparison]::OrdinalIgnoreCase)) {
	throw "Refusing to use a test directory outside the system temp directory: $resolvedTestDirectory"
}

New-Item -ItemType Directory -Path $resolvedTestDirectory | Out-Null
$previousPath = $env:Path
try {
	$disabledConfig = Join-Path $resolvedTestDirectory "disabled.txt"
	$enabledConfig = Join-Path $resolvedTestDirectory "enabled.txt"
	$disabledOutput = Join-Path $resolvedTestDirectory "disabled.wav"
	$enabledOutput = Join-Path $resolvedTestDirectory "enabled.wav"

	$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
	[System.IO.File]::WriteAllText(
		$disabledConfig,
		"LoudnessCorrection: State 0 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0 Volume -38.0",
		$utf8NoBom
	)
	[System.IO.File]::WriteAllText(
		$enabledConfig,
		"LoudnessCorrection: State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0 Volume -38.0",
		$utf8NoBom
	)

	$env:Path = "$runtimeDirectory;$previousPath"
	$commonArguments = @(
		"--nopause",
		"--rate", "48000",
		"--channels", "1",
		"--batchsize", "256",
		"--from", "20",
		"--to", "12500",
		"--length", "1"
	)

	$disabledLog = & $benchmark @commonArguments --config $disabledConfig --output $disabledOutput 2>&1
	if ($LASTEXITCODE -ne 0) {
		throw "Disabled runtime benchmark failed:`n$($disabledLog -join [Environment]::NewLine)"
	}
	$enabledLog = & $benchmark @commonArguments --config $enabledConfig --output $enabledOutput 2>&1
	if ($LASTEXITCODE -ne 0) {
		throw "Enabled runtime benchmark failed:`n$($enabledLog -join [Environment]::NewLine)"
	}

	if (!(Test-Path -LiteralPath $disabledOutput) -or !(Test-Path -LiteralPath $enabledOutput)) {
		throw "Benchmark did not produce both expected WAV files."
	}
	$disabledHash = (Get-FileHash -LiteralPath $disabledOutput -Algorithm SHA256).Hash
	$enabledHash = (Get-FileHash -LiteralPath $enabledOutput -Algorithm SHA256).Hash
	if ($disabledHash -eq $enabledHash) {
		throw "Loudness correction produced the same output as the disabled filter."
	}
	if (($enabledLog -join "`n") -match "samples clipped") {
		throw "The enabled loudness-correction benchmark clipped samples."
	}

	Write-Host "Runtime loudness test passed: enabled output differs and does not clip."
}
finally {
	$env:Path = $previousPath
	if (Test-Path -LiteralPath $resolvedTestDirectory) {
		Remove-Item -LiteralPath $resolvedTestDirectory -Recurse -Force
	}
}
