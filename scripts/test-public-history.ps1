param(
	[string]$Revision = "HEAD"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$disallowedMergeAncestor = "90148767f6363c3b7de5cd22082bb450876d3798"

Push-Location $root
try {
	& git cat-file -e "$Revision^{commit}"
	if ($LASTEXITCODE -ne 0) {
		throw "Could not resolve release revision $Revision."
	}

	$reachableCommits = @(& git rev-list $Revision)
	if ($LASTEXITCODE -ne 0) {
		throw "Could not enumerate commits reachable from $Revision."
	}
	# Compare the reachable commit IDs directly. Unlike merge-base, this also
	# works in a clean origin-only clone where the rejected object was never
	# fetched from the private review remote.
	if ($reachableCommits -contains $disallowedMergeAncestor) {
		throw "The Mixomo exp snapshot must not be reachable from public release history."
	}

	$forbiddenObjects = @()
	# Keep paths in their original Unicode form. Git's default quotePath mode
	# wraps non-ASCII paths in quotes and octal escapes, which would otherwise
	# hide a forbidden directory prefix from the checks below.
	$objectRows = @(& git -c core.quotePath=false rev-list --objects $Revision)
	if ($LASTEXITCODE -ne 0) {
		throw "Could not enumerate objects reachable from $Revision."
	}
	foreach ($row in $objectRows) {
		if ($row -notmatch '^[0-9a-fA-F]{40}\s+(.+)$') {
			continue
		}
		$path = $Matches[1].Replace('\', '/')
		if (($path.StartsWith("IRs/", [StringComparison]::Ordinal) -and
			$path -ne "IRs/README.md") -or
			($path.StartsWith("resources/HeadphoneCalibrations/", [StringComparison]::Ordinal) -and
			$path -ne "resources/HeadphoneCalibrations/README.md")) {
			$forbiddenObjects += $row
		}
	}
	if ($forbiddenObjects.Count -ne 0) {
		throw "Third-party calibration or impulse-response assets are reachable from public history:`n$($forbiddenObjects -join "`n")"
	}

	Write-Host "Public history check passed for $Revision."
}
finally {
	Pop-Location
}
