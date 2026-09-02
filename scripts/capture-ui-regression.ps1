param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Release",
	[string]$QtRoot = "",
	[string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$thirdParty = Join-Path $root "third_party"

if ($QtRoot -eq "") {
	$qtCandidates = Get-ChildItem -Path (Join-Path $thirdParty "Qt") -Filter qmake.exe -Recurse -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -match "\\msvc\d*_64\\bin\\qmake.exe$" } |
		Sort-Object FullName
	if ($qtCandidates.Count -eq 0) {
		throw "Qt qmake.exe not found under third_party\Qt."
	}
	$QtRoot = Split-Path -Parent (Split-Path -Parent $qtCandidates[0].FullName)
}

if ($OutputDirectory -eq "") {
	$OutputDirectory = Join-Path $root "artifacts\ui-regression"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$normalizedRoot = [System.IO.Path]::GetFullPath($root).TrimEnd(
	[System.IO.Path]::DirectorySeparatorChar,
	[System.IO.Path]::AltDirectorySeparatorChar)
$normalizedOutput = $OutputDirectory.TrimEnd(
	[System.IO.Path]::DirectorySeparatorChar,
	[System.IO.Path]::AltDirectorySeparatorChar)
if ($normalizedOutput.Equals($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
	throw "The UI regression output directory must not be the repository root."
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$outputPrefix = $OutputDirectory.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
	[System.IO.Path]::DirectorySeparatorChar
$snapshotRecords = [System.Collections.Generic.List[object]]::new()

$apps = @("Editor", "DeviceSelector", "UpdateChecker")
$expectedBaseSizes = @{
	Editor = @{ Width = 1024; Height = 768 }
	DeviceSelector = @{ Width = 820; Height = 620 }
	UpdateChecker = @{ Width = 640; Height = 380 }
}
$appStates = @{
	Editor = @(
		@{ Label = "default"; FilePrefix = "editor"; SnapshotScenario = ""; Locale = "en" },
		@{ Label = "dense"; FilePrefix = "editor-dense-zh-tw"; SnapshotScenario = "dense"; Locale = "zh_TW" }
	)
	DeviceSelector = @(
		@{ Label = "default"; FilePrefix = "deviceselector"; SnapshotScenario = ""; Locale = "en" }
	)
	UpdateChecker = @(
		@{ Label = "default"; FilePrefix = "updatechecker"; SnapshotScenario = ""; Locale = "en" }
	)
}
$scales = @(
	@{ Label = "100"; Value = "1.0" },
	@{ Label = "125"; Value = "1.25" },
	@{ Label = "150"; Value = "1.5" },
	@{ Label = "175"; Value = "1.75" },
	@{ Label = "200"; Value = "2.0" }
)
$themes = @("light", "dark", "high-contrast")
$fontScales = @(
	@{ Label = "text-150"; Value = "1.5" }
)
$scenarios = @(
	$scales | ForEach-Object {
		@{ Label = $_.Label; DpiScale = $_.Value; FontScale = "1.0" }
	}
	$fontScales | ForEach-Object {
		@{ Label = $_.Label; DpiScale = "1.0"; FontScale = $_.Value }
	}
)
$expectedNames = foreach ($app in $apps) {
	foreach ($state in $appStates[$app]) {
		foreach ($theme in $themes) {
			foreach ($scenario in $scenarios) {
				"$($state.FilePrefix)-$theme-$($scenario.Label).png"
			}
		}
	}
}
if ($expectedNames.Count -ne 72 -or
	@($expectedNames | Select-Object -Unique).Count -ne 72) {
	throw "The UI snapshot matrix must define exactly 72 unique output names."
}

# Only replace files whose complete names are owned by this matrix. Any other
# artifact is preserved and rejected below instead of being deleted silently.
foreach ($expectedName in $expectedNames) {
	$existingSnapshot = Join-Path $OutputDirectory $expectedName
	if (Test-Path -LiteralPath $existingSnapshot) {
		Remove-Item -LiteralPath $existingSnapshot -Force
	}
}
$existingManifest = Join-Path $OutputDirectory "manifest.json"
if (Test-Path -LiteralPath $existingManifest) {
	Remove-Item -LiteralPath $existingManifest -Force
}

# Older revisions placed a disposable executable under the artifact root.
# Remove only that exact, validated child directory while migrating it out.
$legacyPreviewDirectory = [System.IO.Path]::GetFullPath(
	(Join-Path $OutputDirectory ".snapshot-bin"))
if (!$legacyPreviewDirectory.StartsWith($outputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
	throw "Legacy preview directory escaped the output directory: $legacyPreviewDirectory"
}
if (Test-Path -LiteralPath $legacyPreviewDirectory) {
	Remove-Item -LiteralPath $legacyPreviewDirectory -Recurse -Force
}

$qtBin = Join-Path $QtRoot "bin"
$qtPlugins = Join-Path $QtRoot "plugins"
$platformDebugSuffix = if ($Configuration -eq "Debug") { "d" } else { "" }
$requiredPlatformPlugin = Join-Path $qtPlugins "platforms\qwindows$platformDebugSuffix.dll"
if (!(Test-Path -LiteralPath $requiredPlatformPlugin)) {
	throw "Required Qt platform plugin was not found: $requiredPlatformPlugin"
}
$runtimeBin = Join-Path $thirdParty "vcpkg_installed\x64-windows\bin"
$productBin = Join-Path $root "x64\$Configuration"
$snapshotProductBin = Join-Path $root "_build\ui-snapshot-bin-$Configuration"

& (Join-Path $PSScriptRoot "build-qt-apps-x64.ps1") `
	-Configuration $Configuration `
	-QtRoot $QtRoot `
	-EnableUiSnapshots `
	-OutputDirectory $snapshotProductBin
if ($LASTEXITCODE -ne 0) {
	throw "Could not build the test-only UI snapshot applications."
}

function Get-ManifestTool {
	$command = Get-Command mt.exe -ErrorAction SilentlyContinue
	if ($null -ne $command) {
		return $command.Source
	}
	$kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
	$matches = Get-ChildItem -Path $kitsRoot -Filter mt.exe -Recurse -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -match "\\x64\\mt.exe$" } |
		Sort-Object FullName -Descending
	if ($matches.Count -eq 0) {
		throw "Windows SDK mt.exe was not found."
	}
	return $matches[0].FullName
}

function Resolve-AppExecutable([string]$Name) {
	$candidates = @(
		(Join-Path $snapshotProductBin "$Name.exe")
	)
	foreach ($candidate in $candidates) {
		if (Test-Path -LiteralPath $candidate) {
			$resolved = (Resolve-Path -LiteralPath $candidate).Path
			if ($Name -ne "DeviceSelector") {
				return $resolved
			}

			# The shipped selector correctly requires elevation. UI capture uses a
			# disposable copy with an asInvoker manifest so CI never opens UAC.
			$previewDirectory = Join-Path $snapshotProductBin ".as-invoker"
			New-Item -ItemType Directory -Force -Path $previewDirectory | Out-Null
			$preview = Join-Path $previewDirectory "DeviceSelector.snapshot.exe"
			Copy-Item -LiteralPath $resolved -Destination $preview -Force
			$manifestTool = Get-ManifestTool
			$manifest = Join-Path $root "tests\ui-snapshot-as-invoker.manifest"
			& $manifestTool -nologo -manifest $manifest "-outputresource:$preview;#1"
			if ($LASTEXITCODE -ne 0) {
				throw "Could not prepare the non-elevated Device Selector preview."
			}
			return $preview
		}
	}
	throw "$Name.exe was not found. Build the Qt applications first."
}

function Invoke-UiSnapshotCapture {
	param(
		[string]$AppName,
		[string]$Executable,
		[hashtable]$State,
		[string]$Theme,
		[hashtable]$Scenario,
		[string]$Target,
		[string]$DelayMs = "900"
	)

	if (Test-Path -LiteralPath $Target) {
		Remove-Item -LiteralPath $Target -Force
	}

	$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $Executable
	$startInfo.WorkingDirectory = $root
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$startInfo.EnvironmentVariables["QT_QPA_PLATFORM"] = "windows"
	$startInfo.EnvironmentVariables["QT_QPA_PLATFORM_PLUGIN_PATH"] = Join-Path $qtPlugins "platforms"
	$startInfo.EnvironmentVariables["QT_SCALE_FACTOR"] = $Scenario.DpiScale
	$startInfo.EnvironmentVariables["EQAPO_UI_FONT_SCALE"] = $Scenario.FontScale
	$startInfo.EnvironmentVariables["EQAPO_UI_THEME"] = $Theme
	$startInfo.EnvironmentVariables["EQAPO_UI_SNAPSHOT"] = $Target
	$startInfo.EnvironmentVariables["EQAPO_UI_SNAPSHOT_DELAY_MS"] = $DelayMs
	$startInfo.EnvironmentVariables["EQAPO_UI_SNAPSHOT_LOCALE"] = $State.Locale
	# ProcessStartInfo inherits the parent environment. Always overwrite the
	# scenario so a previous manual dense capture cannot contaminate a run.
	$startInfo.EnvironmentVariables["EQAPO_UI_SNAPSHOT_SCENARIO"] = $State.SnapshotScenario
	$startInfo.EnvironmentVariables["PATH"] = "$qtBin;$runtimeBin;$productBin;$($startInfo.EnvironmentVariables['PATH'])"

	$process = [System.Diagnostics.Process]::new()
	$process.StartInfo = $startInfo
	try {
		[void]$process.Start()
		# Drain both redirected pipes concurrently. Waiting before reading can
		# deadlock when Qt diagnostics fill either OS pipe buffer.
		$stdoutTask = $process.StandardOutput.ReadToEndAsync()
		$stderrTask = $process.StandardError.ReadToEndAsync()
		if (!$process.WaitForExit(25000)) {
			$process.Kill()
			if (!$process.WaitForExit(5000)) {
				throw "$AppName could not be terminated after timing out on the Windows platform."
			}
			$stdout = $stdoutTask.GetAwaiter().GetResult()
			$stderr = $stderrTask.GetAwaiter().GetResult()
			if (![string]::IsNullOrWhiteSpace($stdout)) {
				Write-Host $stdout
			}
			if (![string]::IsNullOrWhiteSpace($stderr)) {
				Write-Host $stderr
			}
			throw "$AppName timed out on the Windows platform while capturing $($State.Label)/$Theme/$($Scenario.Label)."
		}
		$stdout = $stdoutTask.GetAwaiter().GetResult()
		$stderr = $stderrTask.GetAwaiter().GetResult()
		if ($process.ExitCode -ne 0) {
			if (![string]::IsNullOrWhiteSpace($stdout)) {
				Write-Host $stdout
			}
			if (![string]::IsNullOrWhiteSpace($stderr)) {
				Write-Host $stderr
			}
			throw "$AppName Windows snapshot failed for $($State.Label)/$Theme/$($Scenario.Label) with exit code $($process.ExitCode)."
		}
	}
	finally {
		$process.Dispose()
	}

	if (!(Test-Path -LiteralPath $Target) -or (Get-Item -LiteralPath $Target).Length -lt 1024) {
		throw "$AppName did not produce a valid Windows snapshot: $Target"
	}

	$bytes = [System.IO.File]::ReadAllBytes($Target)
	$pngSignature = @(137, 80, 78, 71, 13, 10, 26, 10)
	for ($signatureIndex = 0; $signatureIndex -lt $pngSignature.Count; ++$signatureIndex) {
		if ($bytes[$signatureIndex] -ne $pngSignature[$signatureIndex]) {
			throw "$AppName produced a file without a valid PNG signature: $Target"
		}
	}
	$width = ([uint32]$bytes[16] -shl 24) -bor
		([uint32]$bytes[17] -shl 16) -bor
		([uint32]$bytes[18] -shl 8) -bor [uint32]$bytes[19]
	$height = ([uint32]$bytes[20] -shl 24) -bor
		([uint32]$bytes[21] -shl 16) -bor
		([uint32]$bytes[22] -shl 8) -bor [uint32]$bytes[23]
	if ($width -lt 300 -or $height -lt 180) {
		throw "$AppName produced an unexpectedly small snapshot ($width x $height): $Target"
	}

	return [pscustomobject]@{
		Width = [int]$width
		Height = [int]$height
		Sha256 = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash.ToLowerInvariant()
	}
}

$executables = @{}
foreach ($appName in $apps) {
	$executables[$appName] = Resolve-AppExecutable $appName
}

# Snapshot-enabled builds keep their top-level widgets off the physical desktop
# while still using qwindows. This preserves native Windows font rendering and
# prevents the CI runner's work area from clamping the requested viewport.
foreach ($appName in $apps) {
	foreach ($state in $appStates[$appName]) {
		foreach ($theme in $themes) {
			foreach ($scenario in $scenarios) {
			$fileName = "$($state.FilePrefix)-$theme-$($scenario.Label).png"
			$target = [System.IO.Path]::GetFullPath((Join-Path $OutputDirectory $fileName))
			if (!$target.StartsWith($outputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
				throw "Snapshot target escaped the output directory: $target"
			}
			$capture = Invoke-UiSnapshotCapture `
				-AppName $appName `
				-Executable $executables[$appName] `
				-State $state `
				-Theme $theme `
				-Scenario $scenario `
				-Target $target
			if ($scenario.FontScale -eq "1.0") {
				$dpiScale = [double]::Parse(
					[string]$scenario.DpiScale,
					[System.Globalization.CultureInfo]::InvariantCulture)
				$baseSize = $expectedBaseSizes[$appName]
				$expectedWidth = [int][Math]::Round(
					[double]$baseSize["Width"] * $dpiScale,
					[System.MidpointRounding]::AwayFromZero)
				$expectedHeight = [int][Math]::Round(
					[double]$baseSize["Height"] * $dpiScale,
					[System.MidpointRounding]::AwayFromZero)
				if ($capture.Width -ne $expectedWidth -or $capture.Height -ne $expectedHeight) {
					throw "$appName produced $($capture.Width) x $($capture.Height) for $($state.Label)/$theme/$($scenario.Label); expected $expectedWidth x $expectedHeight at DPI scale $($scenario.DpiScale). The snapshot window may have been constrained by the desktop work area."
				}
			}
			$snapshotRecords.Add([pscustomobject]@{
				app = $appName
				state = $state.Label
				locale = $state.Locale
				theme = $theme
				scenario = $scenario.Label
				dpiScale = $scenario.DpiScale
				fontScale = $scenario.FontScale
				file = $fileName
				width = $capture.Width
				height = $capture.Height
				sha256 = $capture.Sha256
			})
			}
		}
	}
}

$artifactFiles = @(Get-ChildItem -LiteralPath $OutputDirectory -File -Recurse)
$expectedFiles = @($artifactFiles | Where-Object {
	$relativeName = $_.FullName.Substring($outputPrefix.Length)
	$expectedNames -contains $relativeName
})
$unexpectedFiles = @($artifactFiles | Where-Object {
	$relativeName = $_.FullName.Substring($outputPrefix.Length)
	$expectedNames -notcontains $relativeName
})
$count = $expectedFiles.Count
if ($count -ne 72 -or $snapshotRecords.Count -ne 72 -or $unexpectedFiles.Count -ne 0) {
	$unexpectedSummary = if ($unexpectedFiles.Count -eq 0) {
		"none"
	} else {
		@($unexpectedFiles | ForEach-Object {
			$_.FullName.Substring($outputPrefix.Length)
		}) -join ", "
	}
	throw "Expected exactly 72 UI snapshots but found $count expected, $($snapshotRecords.Count) manifest records, and $($unexpectedFiles.Count) unexpected file(s): $unexpectedSummary."
}

$manifestPath = Join-Path $OutputDirectory "manifest.json"
$manifestJson = $snapshotRecords | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText(
	$manifestPath,
	$manifestJson + [Environment]::NewLine,
	[System.Text.UTF8Encoding]::new($false))

Write-Host "Captured $count UI snapshots in $OutputDirectory."
