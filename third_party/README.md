# third_party

This folder is the only supported location for third-party source trees and locally built dependency outputs.

The repository currently expects:

- `vst3sdk/` - Steinberg VST 3 SDK headers and interfaces used by the VST3 host support.
- `muparserx/` - expression parser used by Equalizer APO filters.
- `tclap/` - command-line argument parser used by helper tools.
- `vcpkg/` and `vcpkg_installed/` - created by `scripts/bootstrap-third-party.ps1` for FFTW3 and libsndfile.

Generated build folders, package caches, and installed binary outputs under this directory are intentionally ignored by git. Recreate them with:

```powershell
.\scripts\bootstrap-third-party.ps1
```
