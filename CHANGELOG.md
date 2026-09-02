# Changelog

## 3.0.2

- Rebuilt the fork-specific changes directly on the current `Mixomo/EqAPO64_with_VST3_support` main-line history while preserving its original Equalizer APO and VST-hosting foundation.
- Added the explicit `Schema 1 Model FormulaLoudnessV1` marker. Every unmarked loudness entry now remains unchanged and bypassed until Configuration Editor is told either to keep the previously released formula values or convert the original shelf profile.
- Made live filter reloads real-time safe with preallocated buffers, an atomic two-bank handoff, background reclamation, warm-up, and a 100 ms response crossfade.
- Kept automatic volume tracking bound to the selected playback endpoint; missing, changed, capture, or unreadable endpoints fail closed to uncorrected audio and recover through the normal crossfade.
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
