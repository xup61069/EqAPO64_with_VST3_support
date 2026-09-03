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
	$offsetConfig = Join-Path $resolvedTestDirectory "offset-40.txt"
	$legacyConfig = Join-Path $resolvedTestDirectory "legacy-unmarked.txt"
	$disabledOutput = Join-Path $resolvedTestDirectory "disabled.wav"
	$enabledOutput = Join-Path $resolvedTestDirectory "enabled.wav"
	$offsetOutput = Join-Path $resolvedTestDirectory "offset-40.wav"
	$legacyOutput = Join-Path $resolvedTestDirectory "legacy-unmarked.wav"

	$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
	# The generated chirp is exactly full scale.  Give this integration fixture
	# a tiny, shared input margin because the phase-only LR28 identity path can
	# raise the instantaneous crest of a non-stationary chirp by a few ppm.
	# Full-scale steady tones and runtime transitions remain covered separately.
	$integrationInputHeadroom = "Preamp: -0.01 dB`r`n"
	[System.IO.File]::WriteAllText(
		$disabledConfig,
		$integrationInputHeadroom +
			"LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 0 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0 Volume -38.0",
		$utf8NoBom
	)
	[System.IO.File]::WriteAllText(
		$enabledConfig,
		$integrationInputHeadroom +
			"LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0 Volume -38.0",
		$utf8NoBom
	)
	[System.IO.File]::WriteAllText(
		$offsetConfig,
		$integrationInputHeadroom +
			"LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 1 ReferenceLevel 80 ReferenceOffset 40 Attenuation 1.0 Volume -38.0",
		$utf8NoBom
	)
	# This deliberately overlaps the historical, unmarked syntax. It must not
	# be interpreted as the formula model without the explicit marker above.
	[System.IO.File]::WriteAllText(
		$legacyConfig,
		$integrationInputHeadroom +
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
		# The fixed-crossover history gate keeps at least the first second raw;
		# the bounded handoff and correction warm-up can extend activation safely.
		"--length", "2"
	)

	$disabledLog = & $benchmark @commonArguments --config $disabledConfig --output $disabledOutput 2>&1
	if ($LASTEXITCODE -ne 0) {
		throw "Disabled runtime benchmark failed:`n$($disabledLog -join [Environment]::NewLine)"
	}
	$enabledLog = & $benchmark @commonArguments --config $enabledConfig --output $enabledOutput 2>&1
	if ($LASTEXITCODE -ne 0) {
		throw "Enabled runtime benchmark failed:`n$($enabledLog -join [Environment]::NewLine)"
	}
	$offsetLog = & $benchmark @commonArguments --config $offsetConfig --output $offsetOutput 2>&1
	if ($LASTEXITCODE -ne 0) {
		throw "Reference-offset runtime benchmark failed:`n$($offsetLog -join [Environment]::NewLine)"
	}
	$legacyLog = & $benchmark @commonArguments --config $legacyConfig --output $legacyOutput 2>&1
	if ($LASTEXITCODE -ne 0) {
		throw "Legacy fail-closed benchmark failed:`n$($legacyLog -join [Environment]::NewLine)"
	}

	if (!(Test-Path -LiteralPath $disabledOutput) -or
		!(Test-Path -LiteralPath $enabledOutput) -or
		!(Test-Path -LiteralPath $offsetOutput) -or
		!(Test-Path -LiteralPath $legacyOutput)) {
		throw "Benchmark did not produce all expected WAV files."
	}
	$disabledHash = (Get-FileHash -LiteralPath $disabledOutput -Algorithm SHA256).Hash
	$enabledHash = (Get-FileHash -LiteralPath $enabledOutput -Algorithm SHA256).Hash
	$offsetHash = (Get-FileHash -LiteralPath $offsetOutput -Algorithm SHA256).Hash
	$legacyHash = (Get-FileHash -LiteralPath $legacyOutput -Algorithm SHA256).Hash
	if ($disabledHash -eq $enabledHash) {
		throw "Loudness correction produced the same output as the disabled filter."
	}
	if ($offsetHash -eq $enabledHash) {
		throw "ReferenceOffset 0 and ReferenceOffset 40 produced the same runtime output."
	}
	if ($legacyHash -ne $disabledHash) {
		throw "Unmarked legacy loudness settings did not fail closed to bypass."
	}
	if (($enabledLog -join "`n") -match "samples clipped") {
		throw "The enabled loudness-correction benchmark clipped samples:`n$($enabledLog -join [Environment]::NewLine)"
	}

	# Windows PowerShell promotes redirected native stderr records according to
	# ErrorActionPreference.  The transition harness deliberately logs its
	# fail-closed automatic-binding diagnostic to stderr while still succeeding,
	# so capture that diagnostic without allowing it to terminate this script.
	$previousErrorActionPreference = $ErrorActionPreference
	try {
		$ErrorActionPreference = "Continue"
		$transitionLog = & $benchmark --nopause --loudness-transition-test 2>&1
		$transitionExitCode = $LASTEXITCODE
	}
	finally {
		$ErrorActionPreference = $previousErrorActionPreference
	}
	if ($transitionExitCode -ne 0) {
		throw "Dynamic loudness transition regression failed:`n$($transitionLog -join [Environment]::NewLine)"
	}
	$transitionLog | ForEach-Object { Write-Host $_ }

	# These frequencies are prior inter-bin headroom failures. Keep them as
	# fixed-tone regressions so a coarse response scan cannot silently return.
	$safetyCases = @(
		@{
			Name = "48k-low-frequency"
			Rate = 48000
			Frequency = 20.216
			Config = "LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 1 ReferenceLevel 100 ReferenceOffset 0 Attenuation 1.0 Volume -40"
		},
		@{
			Name = "8k-high-frequency"
			Rate = 8000
			Frequency = 3151
			Config = "LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 1 ReferenceLevel 1 ReferenceOffset -100 Attenuation 1.0 Volume 0"
		}
	)

	foreach ($safetyCase in $safetyCases) {
		$safetyConfig = Join-Path $resolvedTestDirectory "$($safetyCase.Name).txt"
		$safetyOutput = Join-Path $resolvedTestDirectory "$($safetyCase.Name).wav"
		[System.IO.File]::WriteAllText(
			$safetyConfig,
			$safetyCase.Config,
			$utf8NoBom
		)

		$safetyArguments = @(
			"--nopause",
			"--rate", $safetyCase.Rate,
			"--channels", "1",
			"--batchsize", "256",
			"--from", $safetyCase.Frequency,
			"--to", $safetyCase.Frequency,
			"--length", "2",
			"--config", $safetyConfig,
			"--output", $safetyOutput
		)
		$safetyLog = & $benchmark @safetyArguments 2>&1
		if ($LASTEXITCODE -ne 0) {
			throw "$($safetyCase.Name) benchmark failed:`n$($safetyLog -join [Environment]::NewLine)"
		}
		if (!(Test-Path -LiteralPath $safetyOutput)) {
			throw "$($safetyCase.Name) benchmark did not produce an output WAV file."
		}
		$safetyLogText = $safetyLog -join "`n"
		if ($safetyLogText -match "samples clipped") {
			throw "$($safetyCase.Name) reproduced clipped samples."
		}
		if ($safetyLogText -notmatch "Max output level:\s+([0-9]+(?:\.[0-9]+)?)") {
			throw "$($safetyCase.Name) benchmark did not report a maximum output level."
		}
		$maximumOutput = [double]::Parse(
			$Matches[1],
			[System.Globalization.CultureInfo]::InvariantCulture
		)
		if ($maximumOutput -gt 1.0) {
			throw "$($safetyCase.Name) exceeded full scale: $maximumOutput"
		}
		Write-Host "$($safetyCase.Name): maximum output $maximumOutput"
	}

	Write-Host "Runtime loudness test passed: ReferenceOffset changes output, legacy settings fail closed, and all safety cases avoid clipping."
}
finally {
	$env:Path = $previousPath
	if (Test-Path -LiteralPath $resolvedTestDirectory) {
		Remove-Item -LiteralPath $resolvedTestDirectory -Recurse -Force
	}
}
