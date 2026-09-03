param(
	[string]$Configuration = "Release",
	[string]$VisualStudioEdition = "",
	[string]$QtRoot = "",
	[switch]$EnableUiSnapshots,
	[string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$thirdParty = Join-Path $root "third_party"
$triplet = "x64-windows"
Import-Module (Join-Path $PSScriptRoot "VisualStudioTools.psm1") -Force

if ($Configuration -notin @("Debug", "Release")) {
	throw "Unsupported configuration '$Configuration'. Use Debug or Release."
}
$qmakeConfiguration = $Configuration.ToLowerInvariant()
$oppositeQmakeConfiguration = if ($Configuration -eq "Debug") { "release" } else { "debug" }
$vcpkgLibSubdirectory = if ($Configuration -eq "Debug") { "debug\lib" } else { "lib" }

if ($QtRoot -eq "") {
	$qtCandidates = Get-ChildItem -Path (Join-Path $thirdParty "Qt") -Filter qmake.exe -Recurse -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -match "\\msvc\d*_64\\bin\\qmake.exe$" } |
		Sort-Object FullName
	if ($qtCandidates.Count -eq 0) {
		throw "Qt qmake.exe not found under third_party\Qt. Run scripts\bootstrap-third-party.ps1 -WithQt first."
	}
	$QtRoot = Split-Path -Parent (Split-Path -Parent $qtCandidates[0].FullName)
}

$qmake = Join-Path $QtRoot "bin\qmake.exe"
$lrelease = Join-Path $QtRoot "bin\lrelease.exe"
$qtTranslations = Join-Path $QtRoot "translations"
if (!(Test-Path -LiteralPath $qmake)) {
	throw "qmake.exe not found: $qmake"
}
if (!(Test-Path -LiteralPath $lrelease)) {
	throw "lrelease.exe not found: $lrelease. Install the Qt Tools archive."
}
if (!(Test-Path -LiteralPath $qtTranslations)) {
	throw "Qt translations directory not found: $qtTranslations"
}

$paths = @{
	LIBSNDFILE_INCLUDE = Join-Path $thirdParty "vcpkg_installed\$triplet\include"
	LIBSNDFILE_LIB = Join-Path $thirdParty "vcpkg_installed\$triplet\$vcpkgLibSubdirectory"
	FFTW_INCLUDE = Join-Path $thirdParty "vcpkg_installed\$triplet\include"
	FFTW_LIB = Join-Path $thirdParty "vcpkg_installed\$triplet\$vcpkgLibSubdirectory"
	MUPARSERX_INCLUDE = Join-Path $thirdParty "muparserx\parser"
	MUPARSERX_LIB = Join-Path $thirdParty "build\muparserx-$triplet\$Configuration"
}

foreach ($path in $paths.GetEnumerator()) {
	if (!(Test-Path -LiteralPath $path.Value)) {
		throw "Missing dependency path $($path.Key): $($path.Value)"
	}
}

$vsDevCmd = Get-VisualStudioDevCmd -Edition $VisualStudioEdition

$apps = @(
	@{ Name = "Editor"; Project = Join-Path $root "Editor\Editor.pro" },
	@{ Name = "DeviceSelector"; Project = Join-Path $root "DeviceSelector\DeviceSelector.pro" },
	@{ Name = "UpdateChecker"; Project = Join-Path $root "UpdateChecker\UpdateChecker.pro" }
)

$outDir = if ($OutputDirectory -ne "") {
	[System.IO.Path]::GetFullPath($OutputDirectory)
} else {
	Join-Path $root "x64\$Configuration"
}
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

foreach ($app in $apps) {
	$appTranslations = Join-Path (Split-Path -Parent $app.Project) "translations"
	foreach ($locale in @("de", "fr", "zh_CN", "zh_TW")) {
		$sourceTranslation = Join-Path $qtTranslations "qtbase_$locale.qm"
		if (!(Test-Path -LiteralPath $sourceTranslation)) {
			throw "Qt base translation not found: $sourceTranslation"
		}
		Copy-Item -LiteralPath $sourceTranslation -Destination $appTranslations -Force
	}

	# Keep Debug and Release intermediates isolated. qmake's `nmake clean`
	# otherwise walks both configurations in a shared tree, which can collide
	# with a PCH still being held by another build.
	$buildLabel = if ($EnableUiSnapshots) { "$Configuration-UiSnapshot" } else { $Configuration }
	$buildDir = Join-Path $root "_build\$($app.Name)-x64-$buildLabel"
	New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
	$snapshotDefine = if ($EnableUiSnapshots) {
		" DEFINES+=EQAPO_ENABLE_UI_SNAPSHOTS"
	} else {
		""
	}

	$cmdParts = @(
		"call `"$vsDevCmd`" -arch=x64 -host_arch=x64",
		"set `"CL=/external:anglebrackets /external:W0`"",
		"`"$lrelease`" `"$($app.Project)`"",
		"set `"LIBSNDFILE_INCLUDE=$($paths.LIBSNDFILE_INCLUDE)`"",
		"set `"LIBSNDFILE_LIB=$($paths.LIBSNDFILE_LIB)`"",
		"set `"FFTW_INCLUDE=$($paths.FFTW_INCLUDE)`"",
		"set `"FFTW_LIB=$($paths.FFTW_LIB)`"",
		"set `"MUPARSERX_INCLUDE=$($paths.MUPARSERX_INCLUDE)`"",
		"set `"MUPARSERX_LIB=$($paths.MUPARSERX_LIB)`"",
		"cd /d `"$buildDir`"",
		"`"$qmake`" `"$($app.Project)`" -spec win32-msvc CONFIG+=$qmakeConfiguration CONFIG-=$oppositeQmakeConfiguration QMAKE_CXXFLAGS_WARN_ON+=/WX$snapshotDefine",
		"nmake clean",
		"nmake"
	)

	cmd /c ($cmdParts -join " && ")
	if ($LASTEXITCODE -ne 0) {
		throw "$($app.Name) build failed with exit code $LASTEXITCODE"
	}

	$exe = Get-ChildItem -LiteralPath $buildDir -Filter "$($app.Name).exe" -Recurse |
		Where-Object { $_.FullName -match "\\$qmakeConfiguration\\" } |
		Select-Object -First 1
	if ($null -eq $exe) {
		throw "$($app.Name).exe was not produced under $buildDir"
	}

	Copy-Item -LiteralPath $exe.FullName -Destination (Join-Path $outDir "$($app.Name).exe") -Force
}

Write-Host "Qt applications are ready in $outDir."
