# Changelog

## 3.0.5

- Added a Windows-native, responsive desktop interface across Configuration Editor, Device Selector, device testing, and Update Checker, with improved high-DPI scaling, keyboard access, screen-reader labels, and translated status feedback.
- Added profile switching, duplication, import/export, device links, temporary bypass, A/B comparison, and tray controls to Configuration Editor while retaining the v3.0.4 loudness-correction runtime and endpoint-binding behavior.
- Improved dense filter editors and plots for small windows and large text, and added automated UI layout and regression checks.

## 3.0.4

- Fixed Configuration Editor analysis so the first displayed response uses the saved loudness-correction settings; changing `ReferenceOffset` now changes the displayed curve immediately instead of being hidden by the realtime cold-start bypass.
- Kept unavailable automatic-volume bindings fail-closed during offline analysis, and added native and runtime regressions that prove `ReferenceOffset` changes processed output while disabled or unavailable filters remain bit-transparent.
- Corrected the Single/Global binding guidance for Matrix-style routing. Global is appropriate only when the Windows default Multimedia master volume is the intended shared control; muted or fixed virtual defaults require Single or manual volume.

## 3.0.3

- Fixed the v3.0.2 installer recovery loop caused by NSIS append mode preserving a file without moving its write pointer to the end. The next installer now retires the corrupted protected manifest, keeps ambiguous `.old` backups untouched, finishes committed recovery cleanup, and continues installation.
- Records renamed application files only after Windows confirms the rename, uses UTF-16 framed records, binds cleanup to each file's stable identity, deletes through the verified handle, and flushes a one-shot cleanup gate before touching product files.
- Added a native NSIS append regression harness to the release build in addition to the installer transaction contract tests.

## 3.0.2

- Rebuilt the fork-specific changes directly on the current `Mixomo/EqAPO64_with_VST3_support` main-line history while preserving its original Equalizer APO and VST-hosting foundation.
- Added the explicit `Schema 1 Model FormulaLoudnessV1` marker. Every unmarked loudness entry now remains unchanged and bypassed until Configuration Editor is told either to keep the previously released formula values or convert the original shelf profile.
- Made live coefficient reloads real-time safe with preallocated banks, atomic publication, background reclamation, and a 100 ms crossfade in the common crossover domain.
- Added explicit Single and Global automatic-volume bindings. Single follows the actual APO playback endpoint and fails closed for capture or unknown flows. Global preserves the original Mixomo behavior: every instance reads the current Windows default Multimedia playback volume without depending on the APO endpoint metadata, for routing systems such as VB-Audio Matrix. A missing endpoint, failed rebind, or unreadable volume source fails closed: correction fades to the uncorrected `A = L + H` path over 10 ms, then recovery silently warms for 250 ms and fades correction back in over 100 ms.
- Kept the settled A-domain magnitude from 1-19 Hz within 0.01 dB of unity with a fixed high-order crossover, without applying the correction branch's headroom reduction. Cold start remains raw for at least 1.0 s and then hands each channel to A only at a bounded sampled crossing; no timeout forces an unsafe handoff.
- Added a persistent installer recovery journal and delayed the transaction commit until application files, permissions, registry data, shortcuts, device selection, and updater configuration have completed. Interrupted upgrades are recovered on the next setup run.
- Gave the installer and updater a fork-specific user-facing identity while retaining the shared Equalizer APO paths and registry keys required for in-place compatibility.
- Added a Traditional Chinese interface and migration guidance, pinned the x64 build/release toolchain, published SHA-256 artifacts, and kept automatic update checks opt-in.
- Presented the feature only as loudness correction, without a standards-conformance, certification, endorsement, affiliation, or approval claim.

## 3.0.1

- Replaced the incorrect loudness data path with the 29-point formula parameter table and direct runtime evaluation.
- Bound automatic volume tracking to the actual playback endpoint selected by Equalizer APO. Capture, missing-endpoint, and endpoint-read failures now bypass correction safely.
- Added a 100 ms dual-bank coefficient crossfade and dense, locally refined peak analysis with a 1 dB headroom margin.
- Preserved the original command and parameter text of 2.0.0 entries until the user explicitly converts them to the enabled formula profile.
- Blocked calibration noise when the selected endpoint is not the Windows default Console playback endpoint, and clarified the one-speaker SPL measurement procedure.
- Made automatic update checks opt-in and strengthened installer cleanup and rollback behavior.
- Added native runtime regressions for difficult low- and high-frequency peak cases. Release installers remain unsigned; verify the published SHA-256 file before installation.
- Described the feature only as loudness correction; no standards-conformance claim is made.

## 3.0.0

- Restored the data-driven 29-band loudness-correction engine, reference-level controls, manual or endpoint-volume tracking, and pink-noise calibration.
- Rebranded the feature as **Loudness Correction**. The project does not claim standards conformance, certification, endorsement, affiliation, or approval.
- Kept the current GitHub update endpoint and installer checksum workflow.
- Restored the `State`, `ReferenceLevel`, `ReferenceOffset`, `Attenuation`, and optional `Volume` configuration format. Existing 2.0.0 loudness-correction entries open as disabled drafts and must be reviewed and recalibrated before enabling.
