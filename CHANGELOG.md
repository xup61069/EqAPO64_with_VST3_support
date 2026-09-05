# Changelog

## Unreleased

- Reduced the default Full loudness engine's settled callback cost with a preallocated section-major block path while retaining the transition fallback and sample-for-sample native equivalence coverage.
- Reduced Full-engine initialization and volume-update cost by reusing each frequency's complex terms and aggregating the correction cascade before magnitude conversion.
- Kept the lower-cost, at-most-two-section Fast engine as an explicitly labeled experimental option because extreme low-level response tests can differ from Full by roughly 10–15 dB.
- Added optional read-only APO volume following for Matrix-style routes that report a Windows volume without applying it to audio. `Off` remains the compatible default; `Linear`, squared `Logarithmic`, and endpoint-dB-based `Windows` curves attenuate the complete post-correction output without writing system volume.
- Smoothed volume-follow changes over 10 ms, mapped endpoint mute to exact silence, kept startup muted until the first valid automatic-volume snapshot, and held the last successful attenuation across transient runtime read failures instead of jumping to full volume.
- Removed the unconditional 1 dB correction-branch headroom margin that made nearly neutral correction start quieter, while retaining peak-derived attenuation and the complete-transfer safety scan.
- Made endpoint notification callbacks own their change signal so a failed Windows callback unregister cannot leave a pointer into a destroyed controller.

## 3.0.7

- Fixed the Configuration Editor startup crash that v3.0.6 could trigger while showing a window after restoring an older Qt toolbar layout.
- Versioned the saved `QMainWindow` layout state so incompatible version-0 toolbar and analysis-panel placement is rejected without touching configuration files, audio settings, profiles, endpoint binding, or other editor preferences.
- Smoothed low-frequency loudness response by optimizing filter quality factor (Q=2.2), eliminating ripples and oscillations between 20 Hz and 100 Hz.
- Corrected high-frequency compensation above 12.5 kHz to follow the natural upward contour using a high-shelf biquad transition, preventing the abrupt cliff drop.
- Fixed excessive vertical height in the Copy Channels editor by binding both tabs to a compact row height.
- Removed the redundant contributor banner from the Loudness Correction panel header for a cleaner layout.
- Kept Reset actions compact without displacing stretch space, restored full row width for the Copy editor, and aligned Preamp and Delay component titles to the top.

## 3.0.6

- Fixed committed installer cleanup so its protected rename manifest remains open on a dedicated handle while old application-file identities are checked. This prevents the first identity check from corrupting the manifest handle or installation-path containment prefix and leaving a false interrupted-installation record.
- Added regression coverage for the handle lifetime and containment state used across multi-record cleanup. Existing committed v3.0.5 journals are resumed without rollback; ambiguous `.old` files remain untouched.
- Restored the intended two-row editor toolbar after loading older saved window states, preventing profile controls from hiding search, comparison, and bypass actions.
- Reduced dense filter-row height by removing the permanent VST compatibility notice, moving the repeated local IR/FIR notice into accessible help, showing manual FIR matching only when it is actionable, and bounding saved Copy-panel heights.
- Replaced the analysis loading bar with a compact text status while retaining numeric headroom, busy-state accessibility, and Auto preamp availability feedback.
- Includes the v3.0.5 Auto preamp command, which performs a fresh identity-bound analysis and can only reduce gain so the current estimated peak does not exceed 0 dB.

## 3.0.5

- Ported the complete Mixomo `exp` feature code onto this fork rather than using its reduced, outdated `main` tree, retaining native audio tools, Parametric EQ, user-supplied headphone calibration and convolution/IR workflows, VST3 class selection, and the experimental out-of-process VST host.
- Added a Windows-native, responsive desktop interface across Configuration Editor, Device Selector, device testing, and Update Checker, with improved high-DPI scaling, keyboard access, screen-reader labels, and translated status feedback.
- Added quick profile access, duplication, import/export, editor-only device links, temporary bypass, A/B comparison, and tray controls to Configuration Editor while retaining the v3.0.4 loudness-correction runtime and endpoint-binding behavior.
- Improved dense filter editors and plots for small windows and large text, and added automated UI layout and regression checks.
- Corrected circular-control direction and added relative vertical dragging with fine adjustment; added an explicit command that returns a floating analysis panel to the bottom dock.
- Added an accessibility-aware response-curve transition and a confirmed one-time Auto preamp action that only reduces gain from a fresh, identity-bound current-file analysis. The action validates the exact root/Include bytes and automatic-volume endpoint snapshot, refuses unsupported dynamic/cross-channel/external processing, cannot widen a scoped edit, and always requires an explicit Save.
- Hardened the restored VU Meter shared-state handoff, reset, multichannel-selector preservation, and sample-peak/ungated-estimate labeling, and improved cold-start/reload ownership for the experimental out-of-process VST host.
- Made Device Test cancellation finish any fallback registration transaction and required Windows audio-service restart already in progress, avoiding a half-applied endpoint state.
- Excluded third-party headphone-measurement catalogs and impulse-response audio from public history and installers until their exact downstream redistribution terms can be established; the corresponding loaders remain available for user-supplied data.

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
