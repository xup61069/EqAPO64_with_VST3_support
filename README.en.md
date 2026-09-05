# Loudness Correction for Equalizer APO

[繁體中文主要說明](README.md)

[![Build](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml/badge.svg)](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/xup61069/loudness-correction-apo)](https://github.com/xup61069/loudness-correction-apo/releases/latest)

This repository is a direct Windows x64 fork of [Mixomo/EqAPO64_with_VST3_support](https://github.com/Mixomo/EqAPO64_with_VST3_support). It retains the system-wide double-precision audio pipeline and x64 VST2/VST3 audio-effect workflow, and maintains formula-based loudness correction, calibration tools, the complete Mixomo `exp` feature line, and a Traditional Chinese interface.

Code lineage: [Equalizer APO](https://sourceforge.net/projects/equalizerapo/) → [TheFireKahuna/equalizerAPO64](https://github.com/TheFireKahuna/equalizerAPO64) → [Mixomo/EqAPO64_with_VST3_support](https://github.com/Mixomo/EqAPO64_with_VST3_support) → this repository.

This fork integrates Mixomo's feature-complete `exp` code line as reviewed source changes. The public history deliberately does not import `exp` as a merge parent because that branch also contains third-party datasets whose downstream redistribution terms have not been established here.

The linked source repository name appears here only for attribution; it is not this project's product name. This repository is not the upstream Equalizer APO project or an official upstream build. The feature is presented only as loudness correction; no standards-conformance, certification, endorsement, affiliation, or approval claim is made.

> This installer replaces an existing Equalizer APO installation in place. It uses the same default installation directory and registry locations and cannot be installed side by side with the upstream release. Back up `config` and any locally installed plug-ins before installing, upgrading, or downgrading.

Release additions, fixes, and compatibility changes are maintained in [CHANGELOG.md](CHANGELOG.md). This README describes the currently available behavior and workflows only.

## Mixomo feature set retained

This fork uses Mixomo's feature-complete `exp` line. The following tools are retained alongside loudness correction:

- the double-precision x64 signal path, in-process VST2/VST3 hosting, and the experimental `OutProcVSTPlugin:` host;
- native Pan, Chorus, Reverb, Crossfeed, Tone Generator, and pass-through VU Meter filters;
- the multi-band `ParametricEQ:` editor with add, remove, sort, reset, import, and export operations;
- Headphone Calibration support for a compatible catalog supplied by the user, with GraphicEQ, parametric-EQ, and FIR export paths;
- Convolution and GraphicEQ FIR workflows with explicit sample-rate checks, matched-FIR regeneration, and local impulse-response discovery; and
- VST diagnostics, multi-class VST3 selection, filter-row clone/reset actions, and the detached out-of-process host lifecycle.

These tools are separate from loudness correction. Their presence does not enable them automatically; add only the filters you intend to use and test at a safe listening level. The public source tree and installer intentionally contain no headphone-measurement catalog or impulse-response audio. Supply only data you are licensed to use: place a compatible `ash_hpcf_catalog.json` under `config\HeadphoneCalibrations`, and local convolution files under `config\IRs`. No dataset is downloaded automatically, and setup/uninstall leaves these user-data subdirectories untouched.

The pass-through VU Meter reports RMS, sample peak, clipping, and ungated loudness estimates. Its saved LUFS label is retained as compatibility metadata but is read-only because the current estimator does not implement the distinct weighting and gating rules implied by those labels; it must not be treated as a compliance meter or true-peak meter.

## Requirements

- Windows 10 version 1809 or later, or Windows 11, on x64 hardware. The release does not include x86 or ARM64 installers. This minimum follows the bundled [Qt 6.10 Windows requirements](https://doc.qt.io/qt-6.10/supported-platforms.html).
- Administrator rights to install the Audio Processing Object (APO) on a Windows audio device.
- A playback or capture endpoint on which Equalizer APO can be enabled.
- The Microsoft Visual C++ 2015–2022 x64 runtime. Setup offers the Microsoft download if it is missing.
- Optional: an SPL meter for acoustic calibration and x64 VST plug-ins for audio-effect hosting.

## Download and verification

> **Install the version marked latest on GitHub Releases.** Download only from this repository's [GitHub Releases](https://github.com/xup61069/loudness-correction-apo/releases/latest); see [CHANGELOG.md](CHANGELOG.md) for version-specific fixes and known issues.

If v3.0.2 says that an interrupted installation could not be recovered, leave its recovery files in place and run v3.0.3 or later. The newer installer safely retires the corrupted v3.0.2 cleanup manifest while preserving any ambiguous `.old` backups; manual registry or `ProgramData` cleanup is not required.

Download the x64 installer and its matching `.sha256` file from the same release. In PowerShell:

```powershell
Get-FileHash .\EqualizerAPO-x64-*.exe -Algorithm SHA256
Get-Content .\EqualizerAPO-x64-*.exe.sha256
```

The two 64-character SHA-256 values must match exactly. The installer, bundled executables and DLLs, release tag, and checksum file are currently unsigned. A matching checksum detects corruption or a mismatch relative to that GitHub Release; it does not independently prove publisher identity.

## Install or upgrade

1. Back up `C:\Program Files\EqualizerAPO\config` and `C:\Program Files\EqualizerAPO\VSTPlugins` if they contain files you need.
2. Close audio tools and run the installer as administrator. Setup may close this project's running utilities and will attempt to create a Windows restore point; installation can continue if Windows declines the restore point.
3. In the Device Selector, enable Equalizer APO only for the playback or capture endpoints that should be processed.
4. Allow setup to restart the Windows audio service. Audio may be interrupted briefly; restart Windows if setup or the Device Selector requests it.
5. Do not force-close setup or power off the computer while files and APO registrations are being updated.

Normal upgrades preserve `config` and a non-empty `VSTPlugins` directory, but a separate backup is still recommended. Automatic update checks are disabled by default.

Setup keeps a persistent recovery journal outside the application directory while replacing program files. If setup is interrupted after the old application tree has been saved, rerun the same or a newer installer; it restores the saved tree before beginning a new transaction. Do not manually delete the recovery data while an installation is incomplete.

After a successful commit, cleanup can remain deferred while Windows still has an old audio file loaded. A recovery record showing `Pending=1` with `Phase=committed` does not by itself mean installation failed; the same or a newer installer will continue the verified cleanup later. Do not delete that recovery state manually.

## Quick start

1. Open **Equalizer APO Configuration Editor** and select the playback endpoint you intend to use.
2. Add **Advanced filters → Loudness correction**.
3. Choose **Single endpoint** to follow the playback endpoint on which this APO instance is running. Choose **Global (Windows default)** only when every loudness-correction instance should deliberately share that default endpoint's master volume.
4. Leave **Manual volume** off for automatic tracking, or enable it when Windows cannot represent the actual listening volume.
5. Set the reference level and correction strength. Use calibration only if a suitable SPL meter is available.
6. Confirm the stored command is enabled and contains `State 1`.

The filter compensates for changes in perceived tonal balance as listening level changes. It is not track loudness normalization, a room-correction system, a hearing test, an automatic microphone measurement, or a limiter.

### Choose the volume source

| Listening setup | Choose | What it follows |
|---|---|---|
| Ordinary output, or each APO endpoint must track its own Windows volume | **Single endpoint** / `Binding Single` | The playback endpoint on which that APO instance is actually running. |
| Every correction instance intentionally shares one Windows volume control | **Global (Windows default)** / `Binding All` | The current Windows default `eRender`/`eMultimedia` endpoint's master volume. |
| Application gain, an analog amplifier, a speaker knob, or another control after the Windows endpoint represents the real level | **Manual volume** | The explicit `Volume` value you maintain. |

For **VB-Audio Matrix**, use `Binding All` only when the Windows default Multimedia endpoint's master volume is deliberately the shared listening-volume control. If the virtual default stays fixed or muted, use `Binding Single` when the actual APO endpoint volume represents listening level, or use manual volume. `Binding All` does **not** install or apply APO processing to every endpoint; Device Selector controls installation, while `Binding` controls only the volume source used by loudness correction.

## Interface and workflow

The Configuration Editor, Device Selector, device-test dialog, and Update Checker follow Windows light, dark, accent-color, and high-contrast settings. Layout regression tests cover 100–200% display scaling, 150% text scaling, and all three color modes. This is a tested compatibility range, not an accessibility-certification claim.

### Configuration profiles and search

- The **Profile** list shows readable top-level `.txt` files in the Equalizer APO `config` directory and refreshes when that directory changes. Opening an item only opens it for editing. The audio engine still starts at `config.txt`; another profile affects audio only when `config.txt` or its `Include` chain references that file.
- The **Profiles** menu can duplicate, rename, import, or export one `.txt` file. Import/export does not bundle files referenced through `Include`, VST plug-ins, or convolution impulses. `config.txt` cannot be renamed, and renaming another profile does not update `Include` statements in other files.
- Search the current filter list with `Ctrl+F`; use `F3` and `Shift+F3` for the next and previous match, and `Esc` to clear the search.
- **Link current profile to selected device** stores an editor convenience for the current Windows user. Selecting that device later opens the linked file in Configuration Editor. It does not install the APO, change routing or `Device:` commands, alter `config.txt`, or change loudness `Binding Single`/`Binding All`.

### Audible A/B and temporary bypass

1. Save the current profile, choose **Capture A**, make the B changes, and save them.
2. Choose **Compare A** / **Return to B** to switch the actual saved file between the captured A and current B contents. The A snapshot belongs only to that profile and editor session.
3. Choose **Bypass** or press `Ctrl+Shift+B` to temporarily comment every active command in the current `.txt`; choose **Restore audio** to restore it. This bypasses the entire file, not only loudness correction.

A/B and bypass require a saved profile with no unsaved edits, cannot run at the same time, and only become audible when that file is in the active `config.txt`/`Include` chain. They temporarily write the profile and therefore use a separate editor recovery journal, distinct from the installer's recovery journal. Normal return, profile changes, or real exit restores the original state. After an interruption, the editor verifies file contents before recovery; if another program changed the file, it asks whether to restore the saved profile or keep the external changes instead of silently overwriting them.

### Notification area and device testing

- Enable **Settings → Keep running in the notification area** if closing the window should hide the editor. Its menu can show the editor, open profiles for editing, toggle Instant mode, bypass or restore the current profile, and exit. A real exit first restores any temporary A/B or bypass state.
- Device Test can restart Windows Audio and try alternative APO registration modes after an initial failure, so audio may be interrupted more than once. Cancelling is cooperative: if a fallback registration transaction has begun, the dialog finishes that transaction and its required Windows Audio restart rather than leaving a half-applied device state.

### Analysis panel, response animation, and Auto preamp

- If the analysis panel is floating or cannot be returned by dragging, choose **View → Dock analysis panel** to restore it to the bottom of Configuration Editor.
- A changed response curve moves to the latest result over about 180 ms. This is display-only: DSP and configuration updates are not delayed, rapid edits replace the older visual target, and the transition is disabled for Windows reduced-animation settings and deterministic UI captures.
- Circular controls use relative vertical dragging instead of jumping to the pointer angle. Drag upward to increase, downward to decrease, or hold `Shift` while dragging for finer control; mouse-wheel and keyboard operation remain available.
- **Auto preamp (current ≤ 0 dB)** becomes available only after a successful current analysis whose device, channel, saved root file, complete `Include` chain, and analysis settings still match the editor byte-for-byte. Automatic loudness correction also has to resolve to the same Windows endpoint and exact volume used by the analysis. After confirmation, it rounds both the required reduction and final target toward attenuation at 0.01 dB precision and lowers an editable root-level `Preamp:` before the first scope boundary. In an unscoped file it can instead insert a new Preamp at the beginning. It never boosts gain, never widens a `Device`/`Channel`/conditional/`Include`/`Stage` scope by inserting above it, and always leaves a dirty edit for explicit review and Save—even when Instant mode is enabled.

Auto preamp is deliberately unavailable for dynamic expressions or conditions, cross-channel processing, convolution dependencies, generated/time-varying/nonlinear processing, VST plug-ins, and experimental out-of-process filters. It targets only the sampled linear frequency-response peak for the selected channel and, when automatic loudness volume is used, the current endpoint-volume snapshot. It is not a limiter and cannot guarantee later volume or source changes, multichannel peaks not represented by the selected channel, or intersample/true peaks. Check the relevant channels and retain additional headroom for real playback.

## Loudness-correction behavior

### Engine modes and performance

- **Full (default):** builds the complete correction from the 29-point formula parameter table spanning 20 Hz through 12.5 kHz. The first 28 available bands use Q=2.2 peaking filters; the 12.5 kHz band uses a Q=0.9 high shelf when available. The settled callback uses a preallocated section-major block path, while transitions retain the sample-major path; native regressions prove sample-for-sample equivalence.
- **Fast (experimental):** uses at most two sections: a 120 Hz low shelf, plus a Q=2 high-frequency peak only when the sample rate can reliably represent 12.5 kHz. It is explicit opt-in behavior, not a numerically equivalent Full replacement. Dense-response tests at extreme low listening levels have shown differences of roughly 10–15 dB; keep Full when response accuracy matters.

Only an intentional Fast selection adds `Engine Fast` to the stored command. Omitting `Engine` preserves Full behavior and the exact text of older configurations.

Neither mode allocates memory in the audio callback. The default Full engine omits center frequencies above 90% of Nyquist at lower sample rates. A fixed 28th-order Linkwitz-Riley crossover forms the common uncorrected `A = L + H` domain, while the fitted correction is applied only to the high-pass contribution. In native extreme-case tests, the settled A-domain magnitude from 1-19 Hz remains within 0.01 dB of unity without inheriting the correction branch's headroom attenuation. The 0.01 dB figure is a settled-magnitude guarantee; the cold raw-to-A handoff may change phase and is governed instead by a one-sample output-step bound. On initialization, the filter outputs the raw input for at least 1.0 s while building crossover history. It then moves each channel into A only at a sampled raw/A crossing whose handoff step is no greater than the larger of the two signals' natural one-sample steps. There is deliberately no timeout: if a safe crossing does not occur, the affected channel remains uncorrected and correction is not enabled. After every channel has entered A, the correction bank warms silently for 250 ms and fades in over 100 ms. Later volume-driven coefficient changes reuse the live crossover history and crossfade between preallocated banks in the A domain over 100 ms.

The current estimated level is:

```text
clamp(ReferenceLevel + Volume - ReferenceOffset, 0, 100)
```

`Volume` is either the source chosen by `Binding` (`Single` = this APO instance's actual playback endpoint; `All` = the Windows default Multimedia playback endpoint) or the explicit manual value. The fitted response is relative to `ReferenceLevel`, so correction is neutral at the reference contour when `Volume` and `ReferenceOffset` are both zero.

For example, with `ReferenceLevel 80` and `Volume -30`, `ReferenceOffset 0` estimates 50 phon. Changing the offset to `+10` estimates 40 phon and requests stronger low-level compensation; changing it to `-10` estimates 60 phon and requests less. The result is clamped to 0–100 phon. Saved offset changes appear immediately in offline analysis; an unavailable automatic volume source still fails closed and remains bypassed.

### Parameters

| Parameter | Range | Meaning |
|---|---:|---|
| `Schema` | `1` | Identifies the versioned parameter layout. |
| `Model` | `FormulaLoudnessV1` | Identifies this formula profile without making a conformance claim. |
| `Engine` | omitted, `Full`, or `Fast` | Omitted or `Full` uses the default complete engine; `Fast` explicitly enables the experimental approximation with at most two sections. Unknown values or duplicate fields fail closed. |
| `Binding` | `Single` or `All` | `Single` follows this APO instance's actual playback endpoint. `All` makes all instances follow the current Windows default Multimedia playback endpoint. Ignored when manual `Volume` is present. |
| `State` | `0` or `1` | Internal bypass or enabled state. New filters use `1`. |
| `ReferenceLevel` | 1–100 phon | Selects the neutral reference contour. The default is 80 phon. |
| `ReferenceOffset` | −100 to +100 dB | Subtracted from the estimated current level. A positive value therefore requests stronger low-level compensation. |
| `Attenuation` | 0–1 | Correction strength: `0` is flat and `1` applies the full fitted correction. |
| `Volume` | −100 to 0 dB | Optional manual volume. Its presence selects manual mode; omitting it selects automatic endpoint tracking. |

### Frequency and level scope

The parameter table ends at 12.5 kHz. Its best-supported range is:

- 20–90 phon from 20 Hz through 4 kHz;
- 20–80 phon from 5 kHz through 12.5 kHz.

The UI permits a 1–100 phon reference and the runtime clamps the calculated current level to 0–100 phon. Values below 20 phon or above the frequency-dependent upper limits are approximate controls, not validated results or a conformance claim. Frequencies above 12.5 kHz are not represented by the profile table.

### Automatic and manual volume

Automatic mode has two explicit bindings:

- **Single endpoint** (`Binding Single`) follows the actual playback endpoint on which that APO instance is running. It never falls back to the Windows default or another device. Use this for ordinary physical outputs and whenever each endpoint must follow its own volume.
- **Global (Windows default)** (`Binding All`) makes every loudness-correction instance follow the master volume of the current Windows default `eRender`/`eMultimedia` endpoint. Use it for VB-Audio Matrix or another Matrix-style routing graph only when that default master volume is the intended shared control. If the virtual default is muted, fixed at its minimum, or is not the control that represents listening level, use `Binding Single` or manual volume instead. The controller checks the default identity at least once every two seconds; after it detects a change, it discards the old endpoint before binding the new one. A failed rebind fails closed through the 10 ms behavior below instead of returning to the old endpoint.

If the required endpoint disappears, is replaced but cannot be rebound, or its volume cannot be read, automatic correction fails closed. Before the cold handoff, output remains raw. Once the A domain is active, only the correction residual is faded to the uncorrected `A = L + H` path over 10 ms. After the configured source recovers, the target bank warms silently for 250 ms and correction fades back in over 100 ms; if the cold handoff is still pending, recovery remains uncorrected until that handoff is safe. `Binding Single` remains bypassed when Windows cannot identify a playback APO context. `Binding All` preserves the original Mixomo behavior by reading the default `eRender`/`eMultimedia` endpoint directly, independently of the current APO's endpoint metadata.

Automatic tracking sees the Windows endpoint volume only. It cannot detect an application's own volume slider, an analog amplifier or speaker knob, or gain changes made after the Windows endpoint. Use manual mode for those systems and update the manual value whenever the real attenuation changes.

`Binding Single` requires an explicit manual `Volume` on capture/input or unidentified endpoints. `Binding All` does not track that APO endpoint; it always uses the Windows default Multimedia playback volume. Built-in calibration-noise playback remains playback-only. When `Volume` is present, it overrides automatic tracking and `Binding` has no runtime effect.

### Calibration

Calibration estimates the 1 kHz listening level at 0 dB tracked volume. It does not measure through a microphone automatically.

1. Start with a safe system or hardware volume. Pink noise can be loud; stop immediately if it is uncomfortable.
2. Use an SPL meter at the listening position, set to **slow response** and **Z weighting (flat)**.
3. Measure only one speaker. Loudness correction is bypassed automatically while the calibration dialog is open.
4. Set the application playing the test to full application volume, play the built-in pink noise, and enter the measured dB SPL value manually.
5. Keep using the same Windows-volume or manual-volume method after saving the calibration. If an external hardware knob controls volume, keep its calibrated position or update the manual value.

The built-in player is available only when the selected endpoint is readable and is also the Windows default **Console** playback endpoint. Global binding additionally requires that endpoint to be the default **Multimedia** playback endpoint, because that is the volume source being calibrated. Otherwise playback is blocked to avoid calibrating the wrong speaker. The destination is checked again immediately before playback; if either required default changes during decoding or playback, the noise is not started or is stopped.

### Headroom

The filter estimates the steady-state peak of the fitted correction branch over a dense frequency grid, refines local maxima, and attenuates that branch by the peak plus a 1 dB margin. It then scans the complete A-domain-plus-correction transfer and reduces the correction branch further if needed. This headroom gain is applied only to the corrected high-pass contribution; the common uncorrected `A = L + H` path is not globally attenuated, so the settled sub-20 Hz magnitude is not pulled down with the correction branch. This reduces clipping risk but is not a sample-peak or true-peak limiter. Transients, multitone signals, later plug-ins, and other gain stages can still clip, so retain additional output headroom where necessary.

## Configuration and migration

An automatic-volume configuration uses:

```text
LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0
```

Adding `Volume -38.0` selects manual mode:

```text
LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0 Volume -38.0
```

For VB-Audio Matrix or another Matrix-style graph whose Windows default Multimedia master volume is the shared control, use `Binding All` and omit `Volume`:

```text
LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding All State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0
```

### Original Mixomo shelf-profile entries

The original Mixomo filter used the same field names for a different shelf-filter model. Because some valid old shelf settings overlap the values written by early formula releases, every unmarked entry is left textually unchanged and bypassed until its meaning is chosen in Configuration Editor. When the values could represent either model, the editor offers both choices. **Convert original shelf profile** preserves the former neutral Windows-volume point by mapping `old ReferenceLevel - old ReferenceOffset` to the new `ReferenceOffset`, maps `Attenuation` to correction strength, preserves a manual volume when present, clamps volume values below −100 dB, selects `Binding All` to retain Mixomo's shared-default-volume behavior, and then enables the marked formula profile. The two response models are not identical, so review and recalibrate after conversion.

### Previously released formula entries

An unmarked formula entry from v3.0.0 or v3.0.1 is also bypassed rather than guessed. Choose **Keep existing formula values** to add the explicit `Schema 1 Model FormulaLoudnessV1` marker with `Binding Single` while preserving its other values and enabled state. If the same numbers are also valid under the original shelf model, the shelf-conversion choice is shown beside it. The marker prevents future or unrelated models from being silently reinterpreted. Marked Schema 1 entries written before `Binding` existed also load as `Single`.

### Entries from v2.0.0

A valid v2.0.0 entry remains textually unchanged and bypassed until **Convert and enable formula profile** is pressed. Conversion:

- maps `NeutralVolumeDb` to `ReferenceOffset`;
- maps `Strength` to `Attenuation`;
- preserves `ManualVolumeDb` when present;
- clamps volume values below −100 dB; and
- replaces the retired headroom mode with automatic headroom.

The converted entry is enabled as `State 1`. Configuration Editor normally saves immediately when Instant mode is enabled, so back up or review the configuration before converting, then recalibrate it for the current system.

### Entries previously opened by v3.0.0

v3.0.0 may already have rewritten an older entry as an unmarked formula-format `State 0 ReferenceLevel ...` draft. First choose **Keep existing formula values** so the editor adds the model marker; this deliberately preserves `State 0`. After backing up the configuration, either close the editor and change only that marked entry's `State 0` to `State 1` in `config\config.txt`, or remove the draft and add a new Loudness correction filter. Review the volume mode and recalibrate before use.

## VST plug-in hosting

The editor can load user-supplied x64 VST2 (`.dll`) and VST3 (`.vst3`) audio effects. **VST plugin** uses the original in-process loader; **Out-of-process VST plugin** uses the experimental detached `EqApoOutProcHost.exe`. The latter can isolate some plug-in failures from Configuration Editor and the APO process, but it is not a security sandbox. No commercial plug-ins are included.

- VST3 support is limited to x64 audio-effect modules. Instruments and MIDI/event-only plug-ins are not supported. When a bundle exposes several compatible effect classes, the editor provides class selection.
- Some plug-ins behave better in one loader than the other. The out-of-process path is experimental, and editor/analyzer state synchronization is intentionally periodic rather than sample-accurate.
- The Windows audio service must be able to read the plug-in and every resource it uses. Copying a plug-in to `C:\Program Files\EqualizerAPO\VSTPlugins` can simplify permissions.
- Some plug-ins depend on a desktop session, copy protection, unsupported bus layouts, or APIs that are unsuitable for a system audio service. Compatibility is not guaranteed.
- Plug-ins run inside the Windows audio-processing path and are not sandboxed. Use only trusted, stable plug-ins, test at a safe volume, and keep a recoverable configuration backup.

## Updates

Selecting automatic update checks during setup creates a scheduled task that runs at sign-in and contacts this repository's GitHub Releases API at most once every 24 hours. It never downloads or installs an update automatically; it only displays a notification and can open the HTTPS release page.

Running the installer again with the option left unchecked removes that scheduled task. You can also use the Start menu **Check for updates** shortcut for a manual check. Release changes are recorded in [CHANGELOG.md](CHANGELOG.md).

The Update Checker can retry a failed check, skip the offered version, or open this repository's GitHub download page. It still never downloads or installs software automatically.

## Uninstall

Use Windows **Installed apps** or the Start menu **Uninstall** shortcut. The uninstaller attempts to remove the update task, endpoint APO registrations, application files, and shortcuts; a restart may be required.

Configuration files and registry backups are preserved unless **Remove configurations and registry backups** is selected. A non-empty `VSTPlugins` directory is left in place. Back up both directories before uninstalling if they contain anything important.

## Troubleshooting

| Symptom | Check |
|---|---|
| No audible processing | Confirm the endpoint is enabled in Device Selector, the command is not commented out, and it contains `State 1`. Restart the Windows audio service or reboot after device-registration changes. |
| Loudness correction remains flat, or `ReferenceOffset` seems ineffective | Confirm the saved command has `State 1`, `Attenuation` is above zero, and the estimated level differs from the reference contour without being pinned at the 0/100 clamp. An unavailable automatic source fails closed; a saved offset change should move the displayed curve immediately when the filter is available. |
| Automatic volume is unavailable or stuck at the floor | For per-device tracking, use `Binding Single` on a readable, identified playback endpoint. Use `Binding All` only if the Windows default Multimedia master volume is the intended shared control; a muted or fixed Matrix endpoint requires `Binding Single` or manual volume. Global mode does not require metadata for the current APO endpoint. |
| The wrong endpoint volume is followed | Use `Binding Single` to follow the APO's actual endpoint. Use `Binding All` only when every instance should deliberately share the Windows default Multimedia volume. |
| A device-linked profile opens but does not affect audio | Device links only open files in Configuration Editor. Ensure `config.txt` or its `Include` chain references that profile; use Device Selector for APO installation and the loudness filter's `Binding` control for its volume source. |
| A/B or bypass is unavailable | Save the profile first and clear unsaved edits. The profile must be in the active `config.txt`/`Include` chain for an audible result, and A/B and bypass cannot be active together. |
| The analysis panel is floating and will not return | Choose **View → Dock analysis panel**. It is restored to the bottom of Configuration Editor. |
| Auto preamp is disabled | Select the intended device and analysis channel, analyze the open saved file, and wait for the result. Save pending edits first and leave A/B and temporary bypass. The action deliberately rejects stale or mismatched analysis results. |
| The editor offers temporary-state recovery | Another session ended while A/B or bypass had temporarily written a profile. Restore the saved profile only if it should replace the current file; otherwise keep the external changes. Do not delete the recovery record manually. |
| Device Test takes time to close after cancellation | A fallback registration transaction may already be in progress. Let it finish the required registration and Windows Audio restart; force-closing it can leave the device state incomplete. |
| Calibration is blocked | Make the selected endpoint the Windows default Console playback device and confirm its volume is readable. With `Binding All`, it must also be the default Multimedia device. Then reopen calibration. |
| Calibration does not follow a hardware knob | Use manual volume and update it when the analog gain changes; Windows cannot observe that knob. |
| A VST plug-in cannot load | Use an x64 audio-effect plug-in and ensure the audio service account can read the plug-in and its external files. |
| Windows warns about the installer | The current releases are unsigned. Download only from this repository and compare the matching SHA-256 file. |

## Build and test from source

Prerequisites:

- Windows x64 with PowerShell;
- Visual Studio 2022 with **Desktop development with C++** and a Windows SDK;
- Git, Python 3, CMake, and internet access for generated dependencies.

The build scripts bootstrap the pinned vcpkg baseline, Qt 6.10.1, and NSIS 3.11 into ignored directories under `third_party`.

```powershell
git clone https://github.com/xup61069/loudness-correction-apo.git
Set-Location .\loudness-correction-apo
python -m unittest discover -s .\tests -p "test_*.py" -v
.\scripts\build-installer-x64.ps1 -Configuration Release
.\scripts\test-runtime-loudness.ps1 -Configuration Release
python -m unittest discover -s .\tests -p "test_outproc_vst_lifecycle.py" -v
.\scripts\capture-ui-regression.ps1 -Configuration Release
git diff --check
```

The installer and checksum are written to `Setup\EqualizerAPO-x64-<version>.exe` and `Setup\EqualizerAPO-x64-<version>.exe.sha256`.

Important source areas:

- `filters/loudnessCorrection/` — formula table, response fitting, endpoint tracking, and runtime DSP;
- `filters/`, `Editor/guis/`, and `EqApoOutProcHost/` — native audio tools, filter controls, VST hosts, conversion, and calibration UI;
- `IRs/` and `resources/HeadphoneCalibrations/` — usage notes and ignored locations for user-supplied impulse responses and compatible headphone-correction catalogs;
- `Setup/` and `scripts/` — installer, dependency bootstrap, staging, and runtime checks;
- `tests/` — formula, safety-contract, translation, installer, update, and release-workflow tests;
- `third_party/` — tracked third-party source and generated dependency locations.

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution rules.

## Security, notices, and license

Only the latest release receives security fixes. Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md).

The installer grants the local Windows Users group Full Control over the shared `config` directory so Configuration Editor can save changes. On a multi-user computer, any local standard user can therefore alter the system-wide audio configuration; account for that in the machine's trust model.

The repository owner has confirmed permission to redistribute the included loudness-profile data and implementation in source and binary form. See [NOTICE.md](NOTICE.md). Public proof of that authorization is not bundled with the repository. That permission does not cover third-party headphone-measurement catalogs or impulse-response audio, which are not included in the public source history or installer.

The program code is distributed under GPL-3.0; see [LICENSE](LICENSE). Tracked and generated dependencies retain their own licenses; see [third_party/README.md](third_party/README.md) and the license files in those source trees before redistributing a custom binary build.
