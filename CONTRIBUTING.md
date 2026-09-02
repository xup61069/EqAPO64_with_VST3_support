# Contributing

Thank you for improving this Equalizer APO fork. Bug fixes, tested device-compatibility improvements, translations, and documentation corrections are welcome.

## Before opening a change

1. Search existing issues and pull requests.
2. Keep changes focused and preserve compatibility with existing Equalizer APO configuration files where practical.
3. Do not commit credentials, generated build directories, installers, proprietary plug-ins, or proprietary source material.
4. New acoustic data must identify its source and confirm that public redistribution is permitted.

## Local checks

Use 64-bit Windows with Git, Python 3, CMake, and Visual Studio's Desktop development with C++ workload.

```powershell
python -m unittest discover -s .\tests -p "test_*.py" -v
.\scripts\build-installer-x64.ps1 -Configuration Release
.\scripts\test-runtime-loudness.ps1 -Configuration Release
git diff --check
```

For DSP changes, describe the expected response, sample rates tested, numerical error, headroom behavior, and any CPU trade-off. For UI changes, check English, `zh_CN`, and `zh_TW`; do not submit unfinished Traditional Chinese strings. Changes to the loudness-profile data or implementation must also update [NOTICE.md](NOTICE.md) when needed.

## Code and commits

- Follow the surrounding C++ and PowerShell style; C++ files in this project generally use tabs.
- Keep real-time audio processing free of allocation, locks that can block, logging, and system API calls.
- Do expensive curve fitting and endpoint queries outside the audio callback.
- Add a regression test for every numerical or parsing bug that can be tested independently.
- Explain user-visible changes in `CHANGELOG.md`.
- Use clear, imperative commit subjects.

## Pull requests

Include the problem, implementation, verification performed, and any remaining limitations. GitHub Actions must pass before merge. By contributing, you agree that your contribution is distributed under this repository's GPL-3.0 license and that you have the right to submit it.
