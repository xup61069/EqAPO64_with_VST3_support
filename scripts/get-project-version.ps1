$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$versionHeader = Join-Path $root "version.h"
$content = Get-Content -LiteralPath $versionHeader -Raw

function Read-VersionPart([string]$Name) {
	$match = [regex]::Match($content, "(?m)^#define\s+$Name\s+(\d+)\s*$")
	if (!$match.Success) {
		throw "Could not read $Name from $versionHeader"
	}
	return [int]$match.Groups[1].Value
}

$major = Read-VersionPart "MAJOR"
$minor = Read-VersionPart "MINOR"
$revision = Read-VersionPart "REVISION"

Write-Output "$major.$minor.$revision"
