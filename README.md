# Equalizer APO 64 — VST3 + Loudness Correction

[繁體中文](README_zh-TW.md)

[![Build](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml/badge.svg)](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/xup61069/loudness-correction-apo)](https://github.com/xup61069/loudness-correction-apo/releases/latest)

An independent Windows x64 fork based on [Equalizer APO](https://sourceforge.net/projects/equalizerapo/) and prior VST3 integration work. It adds VST3 hosting, formula-based loudness correction, calibration tools, and Traditional Chinese UI support.

## Download

Download the installer and matching `.sha256` file from the [latest GitHub release](https://github.com/xup61069/loudness-correction-apo/releases/latest). Verify the checksum before installing.

## Loudness correction

The filter evaluates a 29-point formula parameter table and fits the resulting target response to a 29-band peaking-filter bank. Automatic mode follows the actual playback endpoint selected by Equalizer APO; if that endpoint cannot be identified or read, correction is bypassed instead of following a different device. Filter updates crossfade over 100 ms. A dense, refined peak search reserves an additional 1 dB of headroom, and native runtime regression tests cover difficult low- and high-frequency cases.

The profile evidence is strongest from 20–90 phon through 4 kHz and from 20–80 phon above 4 kHz. Values outside those ranges are provided as an approximate control range, not as a validation or conformance claim.

The editor exposes:

- reference level from 1 to 100 phon;
- reference-level offset and correction strength;
- automatic volume tracking for the selected Windows playback endpoint, or an explicit manual volume; and
- pink-noise calibration for a measured listening position.

An enabled configuration uses this format:

```text
LoudnessCorrection: State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0
```

Add `Volume -38.0` to use an explicit manual volume. Otherwise the filter follows the selected playback endpoint. Inputs and unavailable endpoints require manual volume.

Version 3 uses this configuration format. A 2.0.0 loudness-correction entry uses a different model: the editor preserves its original text and keeps it bypassed until you press the explicit conversion button. Conversion maps the prior neutral volume and strength, then enables the formula profile; review and calibrate it for your system.

The built-in pink-noise player can address only the Windows default playback device used by the Wave API. If that is not the endpoint selected in the editor, playback is blocked so calibration cannot silently use the wrong speaker. Playback is also stopped if the default changes while noise is playing. Automatic update checks are opt-in during installation.

## Notices and license

The repository owner has confirmed permission to redistribute the included loudness-profile data and implementation in source and binary form. The project is presented as loudness correction only: it makes no claim of standards conformance, certification, endorsement, affiliation, or approval. See [NOTICE.md](NOTICE.md).

The program code is available under GPL-3.0; see [LICENSE](LICENSE). The installed copy includes `NOTICE.md` alongside the program files. Release installers are currently not code-signed, so Windows may show an unknown-publisher or SmartScreen warning; verify the accompanying SHA-256 file before installation.
