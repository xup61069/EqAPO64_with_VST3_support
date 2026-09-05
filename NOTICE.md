# Loudness-profile notice

This repository includes a formula-driven loudness-correction implementation and tabular parameter set, plus a separately named implementation of the original Mixomo two-shelf loudness-correction algorithm derived from this fork's declared upstream source lineage.

The original component preserves the algorithm found in Mixomo's initial public import, commit [`3a0cc87`](https://github.com/Mixomo/EqAPO64_with_VST3_support/commit/3a0cc87e1dc73c71158d178729f30dbe679872a9), whose loudness-correction source identifies Alexander Walch as its 2017 copyright holder. The implementation in this repository retains that attribution while replacing unsafe lifecycle and real-time behavior.

The repository owner has confirmed permission to redistribute these included materials publicly in source and binary form. The permission applies to this repository and its released installer packages.

That confirmation covers only the loudness-profile table and its implementation. It does not cover third-party headphone-measurement catalogs or impulse-response audio. The public source history and installer intentionally exclude those datasets; users and private builders must supply only data that they are licensed to use.

This project is presented as a loudness-correction implementation only. It makes no claim of standards conformance, certification, endorsement, affiliation, or approval.

The program code remains subject to the GPL-3.0 license in [LICENSE](LICENSE). This notice accompanies the source repository and installed binary package.
