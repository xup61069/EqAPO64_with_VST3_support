param(
	[string]$Configuration = "Release",
	[string]$PlatformToolset = "",
	[string]$VisualStudioEdition = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$thirdParty = Join-Path $root "third_party"
$triplet = "x64-windows"
Import-Module (Join-Path $root "scripts\VisualStudioTools.psm1") -Force

if ($Configuration -notin @("Debug", "Release")) {
	throw "Unsupported configuration '$Configuration'. Use Debug or Release."
}
$vcpkgLibSubdirectory = if ($Configuration -eq "Debug") { "debug\lib" } else { "lib" }

$paths = @{
	LIBSNDFILE_INCLUDE = Join-Path $thirdParty "vcpkg_installed\$triplet\include"
	LIBSNDFILE_LIB = Join-Path $thirdParty "vcpkg_installed\$triplet\$vcpkgLibSubdirectory"
	FFTW_INCLUDE = Join-Path $thirdParty "vcpkg_installed\$triplet\include"
	FFTW_LIB = Join-Path $thirdParty "vcpkg_installed\$triplet\$vcpkgLibSubdirectory"
	MUPARSERX_INCLUDE = Join-Path $thirdParty "muparserx\parser"
	MUPARSERX_LIB = Join-Path $thirdParty "build\muparserx-$triplet\$Configuration"
	TCLAP_ROOT = Join-Path $thirdParty "tclap"
}

foreach ($path in $paths.GetEnumerator()) {
	if (!(Test-Path -LiteralPath $path.Value)) {
		throw "Missing dependency path $($path.Key): $($path.Value)"
	}
}

$vsDevCmd = Get-VisualStudioDevCmd -Edition $VisualStudioEdition

$windowsKitInclude = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include"
$windowsSdkVersion = ""
if (Test-Path -LiteralPath $windowsKitInclude) {
	$windowsSdkVersion = Get-ChildItem -LiteralPath $windowsKitInclude -Directory |
		Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "um\Windows.h") } |
		Sort-Object Name -Descending |
		Select-Object -First 1 -ExpandProperty Name
}
if ($windowsSdkVersion -eq "") {
	throw "Windows 10 SDK was not found under $windowsKitInclude"
}

# Avoid duplicate PATH/Path process variables confusing MSBuild's CL task.
[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
$systemRoot = $env:SystemRoot
[Environment]::SetEnvironmentVariable("Path", "$systemRoot\System32;$systemRoot;$systemRoot\System32\Wbem", "Process")

$props = @(
	"/p:Configuration=$Configuration",
	"/p:Platform=x64",
	"/p:LIBSNDFILE_INCLUDE=$($paths.LIBSNDFILE_INCLUDE)",
	"/p:LIBSNDFILE_LIB=$($paths.LIBSNDFILE_LIB)",
	"/p:FFTW_INCLUDE=$($paths.FFTW_INCLUDE)",
	"/p:FFTW_LIB=$($paths.FFTW_LIB)",
	"/p:MUPARSERX_INCLUDE=$($paths.MUPARSERX_INCLUDE)",
	"/p:MUPARSERX_LIB=$($paths.MUPARSERX_LIB)",
	"/p:TCLAP_ROOT=$($paths.TCLAP_ROOT)",
	"/p:TreatWarningAsError=true",
	"/p:WindowsTargetPlatformVersion=$windowsSdkVersion",
	"/m"
)
if ($PlatformToolset -ne "") {
	$props += "/p:PlatformToolset=$PlatformToolset"
}

$commands = @(
	"msbuild Common.vcxproj $($props -join ' ')",
	"msbuild EqualizerAPO\EqualizerAPO.vcxproj $($props -join ' ')",
	"msbuild EqApoOutProcHost\EqApoOutProcHost.vcxproj $($props -join ' ')",
	"msbuild Benchmark\Benchmark.vcxproj $($props -join ' ')",
	"msbuild VoicemeeterClient\VoicemeeterClient.vcxproj $($props -join ' ')"
)

$cmd = "set WindowsTargetPlatformVersion=$windowsSdkVersion && set WindowsSDKVersion=$windowsSdkVersion\ && call `"$vsDevCmd`" -winsdk=$windowsSdkVersion && " + ($commands -join " && ")
Push-Location $root
try {
	cmd /c $cmd
	if ($LASTEXITCODE -ne 0) {
		throw "Native x64 build failed with exit code $LASTEXITCODE"
	}
}
finally {
	Pop-Location
}
