# Equalizer APO 64 — VST3 + Loudness Correction

[繁體中文](README_zh-TW.md)

[![Build](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml/badge.svg)](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/xup61069/loudness-correction-apo)](https://github.com/xup61069/loudness-correction-apo/releases/latest)

An independent Windows x64 fork based on [Equalizer APO](https://sourceforge.net/projects/equalizerapo/) and prior VST3 integration work. It adds VST3 hosting, a data-driven loudness-correction filter, calibration tools, and Traditional Chinese UI support.

## Download

Download the installer and matching `.sha256` file from the [latest GitHub release](https://github.com/xup61069/loudness-correction-apo/releases/latest). Verify the checksum before installing.

## Loudness correction

The filter uses a 29-band, data-driven loudness profile. It adjusts the response as listening volume changes while fitting the target response to the actual peaking-filter bank and reserving headroom to avoid clipping.

The editor exposes:

- reference level from 1 to 100 phon;
- reference-level offset and correction strength;
- automatic Windows endpoint-volume tracking or an explicit manual volume; and
- pink-noise calibration for a measured listening position.

An enabled configuration uses this format:

```text
LoudnessCorrection: State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0
```

Add `Volume -38.0` to use an explicit manual volume. Otherwise the filter follows the Windows endpoint volume.

Version 3 restores this configuration format. A 2.0.0 loudness-correction entry uses a different model; when opened in the editor it becomes a disabled safe draft. Review its volume mode, set the reference level, and calibrate before enabling it.

## Notices and license

The repository owner has confirmed permission to redistribute the included loudness-profile data and implementation in source and binary form. The project is presented as loudness correction only: it makes no claim of standards conformance, certification, endorsement, affiliation, or approval. See [NOTICE.md](NOTICE.md).

The program code is available under GPL-3.0; see [LICENSE](LICENSE). The installed copy includes `NOTICE.md` alongside the program files.
