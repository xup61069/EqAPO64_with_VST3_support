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
	foreach ($theme in $themes) {
		foreach ($scenario in $scenarios) {
			"$($app.ToLowerInvariant())-$theme-$($scenario.Label).png"
		}
	}
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

foreach ($appName in $apps) {
	$executable = Resolve-AppExecutable $appName
	foreach ($theme in $themes) {
		foreach ($scenario in $scenarios) {
			$fileName = "$($appName.ToLowerInvariant())-$theme-$($scenario.Label).png"
			$target = [System.IO.Path]::GetFullPath((Join-Path $OutputDirectory $fileName))
			if (!$target.StartsWith($outputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
				throw "Snapshot target escaped the output directory: $target"
			}
			if (Test-Path -LiteralPath $target) {
				Remove-Item -LiteralPath $target -Force
			}

			$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
			$startInfo.FileName = $executable
			$startInfo.WorkingDirectory = $root
			$startInfo.UseShellExecute = $false
			$startInfo.CreateNoWindow = $true
			$startInfo.RedirectStandardOutput = $true
			$startInfo.RedirectStandardError = $true
			$startInfo.EnvironmentVariables["QT_QPA_PLATFORM"] = "windows"
			$startInfo.EnvironmentVariables["QT_QPA_PLATFORM_PLUGIN_PATH"] = Join-Path $qtPlugins "platforms"
			$startInfo.EnvironmentVariables["QT_SCALE_FACTOR"] = $scenario.DpiScale
			$startInfo.EnvironmentVariables["EQAPO_UI_FONT_SCALE"] = $scenario.FontScale
			$startInfo.EnvironmentVariables["EQAPO_UI_THEME"] = $theme
			$startInfo.EnvironmentVariables["EQAPO_UI_SNAPSHOT"] = $target
			$startInfo.EnvironmentVariables["EQAPO_UI_SNAPSHOT_DELAY_MS"] = "900"
			$startInfo.EnvironmentVariables["PATH"] = "$qtBin;$runtimeBin;$productBin;$($startInfo.EnvironmentVariables['PATH'])"

			$process = [System.Diagnostics.Process]::new()
			$process.StartInfo = $startInfo
			[void]$process.Start()
			if (!$process.WaitForExit(25000)) {
				$process.Kill($true)
				throw "$appName timed out while capturing $theme/$($scenario.Label)."
			}
			$stdout = $process.StandardOutput.ReadToEnd()
			$stderr = $process.StandardError.ReadToEnd()
			if ($process.ExitCode -ne 0) {
				throw "$appName snapshot failed with exit code $($process.ExitCode).`n$stdout`n$stderr"
			}
			if (!(Test-Path -LiteralPath $target) -or (Get-Item -LiteralPath $target).Length -lt 1024) {
				throw "$appName did not produce a valid snapshot: $target"
			}

			$bytes = [System.IO.File]::ReadAllBytes($target)
			$pngSignature = @(137, 80, 78, 71, 13, 10, 26, 10)
			for ($signatureIndex = 0; $signatureIndex -lt $pngSignature.Count; ++$signatureIndex) {
				if ($bytes[$signatureIndex] -ne $pngSignature[$signatureIndex]) {
					throw "$appName produced a file without a valid PNG signature: $target"
				}
			}
			$width = ([uint32]$bytes[16] -shl 24) -bor
				([uint32]$bytes[17] -shl 16) -bor
				([uint32]$bytes[18] -shl 8) -bor [uint32]$bytes[19]
			$height = ([uint32]$bytes[20] -shl 24) -bor
				([uint32]$bytes[21] -shl 16) -bor
				([uint32]$bytes[22] -shl 8) -bor [uint32]$bytes[23]
			if ($width -lt 300 -or $height -lt 180) {
				throw "$appName produced an unexpectedly small snapshot ($width x $height): $target"
			}
			$snapshotRecords.Add([pscustomobject]@{
				app = $appName
				theme = $theme
				scenario = $scenario.Label
				dpiScale = $scenario.DpiScale
				fontScale = $scenario.FontScale
				file = $fileName
				width = $width
				height = $height
				sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
			})
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
if ($count -ne ($apps.Count * $themes.Count * $scenarios.Count) -or $unexpectedFiles.Count -ne 0) {
	$unexpectedSummary = if ($unexpectedFiles.Count -eq 0) {
		"none"
	} else {
		@($unexpectedFiles | ForEach-Object {
			$_.FullName.Substring($outputPrefix.Length)
		}) -join ", "
	}
	throw "Expected exactly 54 UI snapshots but found $count expected and $($unexpectedFiles.Count) unexpected file(s): $unexpectedSummary."
}

$manifestPath = Join-Path $OutputDirectory "manifest.json"
$manifestJson = $snapshotRecords | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText(
	$manifestPath,
	$manifestJson + [Environment]::NewLine,
	[System.Text.UTF8Encoding]::new($false))

Write-Host "Captured $count UI snapshots in $OutputDirectory."
