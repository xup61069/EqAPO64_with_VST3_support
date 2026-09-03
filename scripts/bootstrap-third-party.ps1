param(
    [string] $Triplet = "x64-windows",
    [string] $Configuration = "Release",
    [switch] $WithQt,
    [string] $QtVersion = "6.10.1",
    [string] $QtArch = "win64_msvc2022_64",
    [string] $AqtInstallVersion = "3.3.0",
    [string] $AqtInstallSha256 = "E88DBD87226F276FDD5D05347A44578D390D93A7F176E9476FBDA0A7C9635F69",
    [switch] $WithNsis,
    [string] $NsisVersion = "3.11",
    [string] $NsisSha256 = "C7D27F780DDB6CFFB4730138CD1591E841F4B7EDB155856901CDF5F214394FA1",
    [string] $VcpkgCommit = "30ef65cad98f08e7197c9a1656fbd871bcb72f2d"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$thirdParty = Join-Path $root "third_party"
New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name was not found in PATH"
    }
}

Require-Command git
Require-Command cmake
Require-Command python

$vendoredDependencies = @(
    @{ Name = "VST3 SDK"; Path = Join-Path $thirdParty "vst3sdk" },
    @{ Name = "muparserx"; Path = Join-Path $thirdParty "muparserx" },
    @{ Name = "TCLAP"; Path = Join-Path $thirdParty "tclap" }
)

foreach ($dependency in $vendoredDependencies) {
    if (!(Test-Path -LiteralPath $dependency.Path)) {
        throw "$($dependency.Name) is a tracked dependency but is missing at $($dependency.Path). Restore it from Git."
    }
}

$vcpkgRoot = Join-Path $thirdParty "vcpkg"
if (!(Test-Path -LiteralPath $vcpkgRoot)) {
    git clone --filter=blob:none --no-checkout https://github.com/microsoft/vcpkg.git $vcpkgRoot
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg clone failed with exit code $LASTEXITCODE"
    }
}
if (!(Test-Path -LiteralPath (Join-Path $vcpkgRoot ".git"))) {
    throw "The generated vcpkg directory is not a Git checkout: $vcpkgRoot"
}
git -C $vcpkgRoot fetch --depth 1 origin $VcpkgCommit
if ($LASTEXITCODE -ne 0) {
    throw "Could not fetch pinned vcpkg commit $VcpkgCommit"
}
git -C $vcpkgRoot checkout --detach $VcpkgCommit
if ($LASTEXITCODE -ne 0) {
    throw "Could not check out pinned vcpkg commit $VcpkgCommit"
}

$vcpkgExe = Join-Path $thirdParty "vcpkg\vcpkg.exe"
& (Join-Path $thirdParty "vcpkg\bootstrap-vcpkg.bat") -disableMetrics
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $vcpkgExe)) {
    throw "vcpkg bootstrap failed with exit code $LASTEXITCODE"
}

$vcpkgInstallRoot = Join-Path $thirdParty "vcpkg_installed"
& $vcpkgExe install --triplet $Triplet --x-install-root=$vcpkgInstallRoot
if ($LASTEXITCODE -ne 0) {
    throw "vcpkg install failed with exit code $LASTEXITCODE"
}

$muparserBuild = Join-Path $thirdParty "build\muparserx-$Triplet"
$muparserSource = Join-Path $thirdParty "muparserx"
$muparserLib = Join-Path $muparserBuild "$Configuration\muparserx.lib"
if (Test-Path -LiteralPath $muparserLib) {
    Write-Host "muparserx already built: $muparserLib"
} else {
    cmake -S $muparserSource -B $muparserBuild -A x64 -DUSE_WIDE_STRING=ON -DCMAKE_BUILD_TYPE=$Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "muparserx configure failed with exit code $LASTEXITCODE"
    }
    cmake --build $muparserBuild --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "muparserx build failed with exit code $LASTEXITCODE"
    }
}

if ($WithQt) {
    $qtRoot = Join-Path $thirdParty "Qt"
    $qtHost = Join-Path $qtRoot "$QtVersion\msvc2022_64"
    $qmake = Join-Path $qtHost "bin\qmake.exe"
    $windeployqt = Join-Path $qtHost "bin\windeployqt.exe"
    $lrelease = Join-Path $qtHost "bin\lrelease.exe"
	$lupdate = Join-Path $qtHost "bin\lupdate.exe"
	$qtQml = Join-Path $qtHost "bin\Qt6Qml.dll"
	$qtTranslationCatalog = Join-Path $qtHost "translations\catalogs.json"

    if ((Test-Path -LiteralPath $qmake) -and
        (Test-Path -LiteralPath $windeployqt) -and
		(Test-Path -LiteralPath $lrelease) -and
		(Test-Path -LiteralPath $lupdate) -and
		(Test-Path -LiteralPath $qtQml) -and
		(Test-Path -LiteralPath $qtTranslationCatalog)) {
        Write-Host "Qt already exists: $qtHost"
    } else {
        $pythonTools = Join-Path $thirdParty "python"
        New-Item -ItemType Directory -Force -Path $pythonTools | Out-Null

        # Pin the aqtinstall wheel itself by content. Its transitive Python
        # packages and the Qt archives fetched by aqt are not content-locked,
        # so release builds using this path must keep a read-only token.
        if ($AqtInstallSha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw "AqtInstallSha256 must be a 64-character SHA-256 digest"
        }
        $aqtDownloadRoot = Join-Path $thirdParty "downloads\aqtinstall"
        New-Item -ItemType Directory -Force -Path $aqtDownloadRoot | Out-Null
        $aqtWheel = Join-Path $aqtDownloadRoot "aqtinstall-$AqtInstallVersion-py3-none-any.whl"
        if (!(Test-Path -LiteralPath $aqtWheel)) {
            python -m pip download --disable-pip-version-check --no-deps --only-binary=:all: --dest $aqtDownloadRoot "aqtinstall==$AqtInstallVersion"
            if ($LASTEXITCODE -ne 0) {
                throw "aqtinstall wheel download failed with exit code $LASTEXITCODE"
            }
        }
        if (!(Test-Path -LiteralPath $aqtWheel)) {
            throw "aqtinstall wheel was not downloaded to the expected path: $aqtWheel"
        }
        $actualAqtSha256 = (Get-FileHash -LiteralPath $aqtWheel -Algorithm SHA256).Hash
        if (![StringComparer]::OrdinalIgnoreCase.Equals($actualAqtSha256, $AqtInstallSha256)) {
            throw "aqtinstall SHA-256 mismatch. Expected $AqtInstallSha256 but received $actualAqtSha256"
        }

        python -m pip install --disable-pip-version-check --upgrade --only-binary=:all: --target $pythonTools $aqtWheel
        if ($LASTEXITCODE -ne 0) {
            throw "aqtinstall install failed with exit code $LASTEXITCODE"
        }

        $env:PYTHONPATH = $pythonTools
        $qtInstalled = $false
        for ($attempt = 1; $attempt -le 3; $attempt++) {
			python -m aqt install-qt windows desktop $QtVersion $QtArch -O $qtRoot --archives qtbase qtdeclarative qtsvg qttools qttranslations
            if ($LASTEXITCODE -eq 0) {
                $qtInstalled = $true
                break
            }
            if ($attempt -lt 3) {
                Write-Warning "Qt download attempt $attempt failed; retrying in 5 seconds."
                Start-Sleep -Seconds 5
            }
        }
        if (!$qtInstalled) {
            throw "Qt install failed after 3 attempts."
        }
    }
}

if ($WithNsis) {
    $nsisRoot = Join-Path $thirdParty "nsis-$NsisVersion"
    $makensis = Join-Path $nsisRoot "makensis.exe"
    if (Test-Path -LiteralPath $makensis) {
        Write-Host "NSIS already exists: $nsisRoot"
    } else {
        Require-Command curl.exe
        $zip = Join-Path $thirdParty "nsis-$NsisVersion.zip"
        $downloadZip = "$zip.download"
        if ($NsisSha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw "NsisSha256 must be a 64-character SHA-256 digest"
        }

        try {
            curl.exe -L --fail --retry 3 --proto "=https" --proto-redir "=https" --tlsv1.2 --output $downloadZip "https://sourceforge.net/projects/nsis/files/NSIS%203/$NsisVersion/nsis-$NsisVersion.zip/download"
            if ($LASTEXITCODE -ne 0) {
                throw "NSIS download failed with exit code $LASTEXITCODE"
            }

            $actualNsisSha256 = (Get-FileHash -LiteralPath $downloadZip -Algorithm SHA256).Hash
            if (![StringComparer]::OrdinalIgnoreCase.Equals($actualNsisSha256, $NsisSha256)) {
                throw "NSIS SHA-256 mismatch. Expected $NsisSha256 but received $actualNsisSha256"
            }

            Move-Item -LiteralPath $downloadZip -Destination $zip -Force
        }
        finally {
            if (Test-Path -LiteralPath $downloadZip) {
                Remove-Item -LiteralPath $downloadZip -Force
            }
        }

        Expand-Archive -LiteralPath $zip -DestinationPath $thirdParty -Force
        if (!(Test-Path -LiteralPath $makensis)) {
            throw "NSIS was not extracted to the expected path: $makensis"
        }
    }
}

Write-Host "third_party is ready."
