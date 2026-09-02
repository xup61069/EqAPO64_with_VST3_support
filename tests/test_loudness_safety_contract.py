#!/usr/bin/env python3
"""Regression contracts for endpoint binding and safe loudness updates."""

from __future__ import annotations

import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
FILTER_ENGINE = (ROOT / "FilterEngine.cpp").read_text(encoding="utf-8")
FILTER_HEADER = (
    ROOT / "filters" / "loudnessCorrection" / "LoudnessCorrectionFilter.h"
).read_text(encoding="utf-8")
FILTER_SOURCE = (
    ROOT / "filters" / "loudnessCorrection" / "LoudnessCorrectionFilter.cpp"
).read_text(encoding="utf-8")
VOLUME_SOURCE = (
    ROOT / "filters" / "loudnessCorrection" / "VolumeController.cpp"
).read_text(encoding="utf-8")
BENCHMARK_SOURCE = (ROOT / "Benchmark" / "Benchmark.cpp").read_text(
    encoding="utf-8"
)
RUNTIME_TEST_SOURCE = (
    ROOT / "scripts" / "test-runtime-loudness.ps1"
).read_text(encoding="utf-8")
GUI_PATH = ROOT / "Editor" / "guis" / "LoudnessCorrectionFilterGUI.cpp"
CALIBRATION_PATH = (
    ROOT / "Editor" / "guis" / "LoudnessCorrectionFilterGUIDialog.cpp"
)
GUI_FACTORY_PATH = (
    ROOT / "Editor" / "guis" / "LoudnessCorrectionFilterGUIFactory.cpp"
)
LEGACY_GUI_PATH = (
    ROOT / "Editor" / "guis" / "LegacyLoudnessCorrectionFilterGUI.cpp"
)
RELEASE_CONTRACT_PATHS = tuple(
    ROOT / name
    for name in (
        "version.h",
        "vcpkg.json",
        ".github/workflows/release.yml",
        "README.md",
        "README_zh-TW.md",
        "NOTICE.md",
        "CHANGELOG.md",
    )
)


class LoudnessSafetyContractTests(unittest.TestCase):
    def test_formula_parameters_are_versioned_and_legacy_values_fail_closed(self) -> None:
        self.assertIn('archive.add(1, L"Schema")', FILTER_HEADER)
        self.assertIn('L"FormulaLoudnessV1"', FILTER_HEADER)
        self.assertIn("if (!(isFormulaSchema && isFormulaModel))", FILTER_HEADER)
        self.assertIn("if (referenceLevel <= 0.0f)", FILTER_HEADER)
        self.assertNotIn("referenceLevel = 80.0f;", FILTER_HEADER)

    def test_runtime_context_is_applied_before_filter_initialization(self) -> None:
        context_index = FILTER_ENGINE.index("filter->setRuntimeContext(runtimeContext)")
        initialize_index = FILTER_ENGINE.index("filter->initialize(")
        self.assertLess(context_index, initialize_index)
        self.assertIn("runtimeContext.endpointId = deviceGuid;", FILTER_ENGINE)
        self.assertNotIn('L"{0.0.0.00000000}."', FILTER_ENGINE)
        self.assertNotIn('L"{0.0.1.00000000}."', FILTER_ENGINE)

    def test_volume_tracking_never_falls_back_to_another_device(self) -> None:
        self.assertIn(
            "deviceEnumerator->GetDevice(_requestedEndpointId.c_str(), &device)",
            VOLUME_SOURCE,
        )
        self.assertIn("EnumAudioEndpoints(", VOLUME_SOURCE)
        self.assertIn("ENDPOINT_GUID_PROPERTY", VOLUME_SOURCE)
        self.assertIn("candidate->OpenPropertyStore(", VOLUME_SOURCE)
        self.assertIn("device->GetId(&endpointId)", VOLUME_SOURCE)
        self.assertNotIn("waveOutGetVolume", VOLUME_SOURCE)
        self.assertNotIn("GetMasterVolumeLevelScalar", VOLUME_SOURCE)

    def test_missing_or_unreadable_endpoint_bypasses_runtime_filter(self) -> None:
        self.assertIn("_runtimeContext.isCapture", FILTER_SOURCE)
        self.assertGreaterEqual(
            FILTER_SOURCE.count("_runtimeBypass.store(true"),
            3,
        )
        self.assertIn("_runtimeBypass.load(std::memory_order_acquire)", FILTER_SOURCE)
        self.assertIn(
            "VolumeController volumeController(self->_runtimeContext.endpointId)",
            FILTER_SOURCE,
        )
        self.assertIn(
            "could not create the endpoint-volume tracking event",
            FILTER_SOURCE,
        )
        self.assertIn("_recoveryPending", FILTER_HEADER)
        self.assertIn("_transitionFromBypass", FILTER_HEADER)
        self.assertIn("if (!recovering && std::isfinite(lastVolume)", FILTER_SOURCE)
        self.assertIn("crossfade from the unfiltered signal", FILTER_SOURCE)

    def test_dynamic_coefficients_use_preallocated_crossfade_banks(self) -> None:
        self.assertIn("_biquadBanks[2]", FILTER_HEADER)
        self.assertIn("COEFFICIENT_CROSSFADE_SECONDS = 0.1", FILTER_HEADER)
        self.assertIn("TryEnterCriticalSection", FILTER_SOURCE)
        self.assertIn("resetState()", FILTER_SOURCE)
        self.assertIn("_warmupActive", FILTER_SOURCE)
        self.assertIn("_crossfadeActive", FILTER_SOURCE)
        self.assertIn("inputChannel[frame] * _targetOutputGainLinear", FILTER_SOURCE)
        process_body = FILTER_SOURCE.split(
            "void LoudnessCorrectionFilter::process", maxsplit=1
        )[1]
        self.assertNotIn(".resize(", process_body)
        self.assertNotIn(".push_back(", process_body)
        self.assertIn('"loudness-transition-test"', BENCHMARK_SOURCE)
        self.assertIn("runLoudnessParameterCodecTests", BENCHMARK_SOURCE)
        self.assertIn("Loudness parameter codec", BENCHMARK_SOURCE)
        self.assertIn("mixomoLegacy.isInitialized()", BENCHMARK_SOURCE)
        self.assertIn("!releasedFormula.isInitialized()", BENCHMARK_SOURCE)
        self.assertIn("unknownModel.isInitialized()", BENCHMARK_SOURCE)
        self.assertIn("truncated.isInitialized()", BENCHMARK_SOURCE)
        self.assertIn("duplicate.isInitialized()", BENCHMARK_SOURCE)
        self.assertIn("8k-minus100-to-0", BENCHMARK_SOURCE)
        self.assertIn("8k-0-to-minus100", BENCHMARK_SOURCE)
        self.assertIn("runLoudnessRecoveryCase", BENCHMARK_SOURCE)
        self.assertIn("Runtime bypass was not bit-transparent", BENCHMARK_SOURCE)
        self.assertIn("--loudness-transition-test", RUNTIME_TEST_SOURCE)

    @unittest.skipUnless(
        GUI_PATH.is_file() and CALIBRATION_PATH.is_file() and LEGACY_GUI_PATH.is_file(),
        "editor safety slice has not been added yet",
    )
    def test_editor_requires_readable_endpoint_and_rechecks_calibration(self) -> None:
        gui_source = GUI_PATH.read_text(encoding="utf-8")
        calibration_source = CALIBRATION_PATH.read_text(encoding="utf-8")
        self.assertIn("FAILED(volumeController->getVolume(endpointVolume))", gui_source)
        self.assertIn('tr("Manual volume (required):")', gui_source)
        play_handler = calibration_source.split(
            "void LoudnessCorrectionFilterGUIDialog::on_playButton_clicked()",
            maxsplit=1,
        )[1]
        self.assertLess(
            play_handler.index("isDefaultWaveRenderEndpoint(endpointId)"),
            play_handler.index("PlaySoundA("),
        )
        self.assertIn("GetDefaultAudioEndpoint(eRender, eConsole", calibration_source)
        self.assertIn("endpointGuardTimer.start(250)", calibration_source)
        self.assertIn("&QTimer::timeout", calibration_source)
        self.assertIn("&& !tryUpdateVolume())", gui_source)
        self.assertIn('tr("Calibration not applied")', gui_source)
        self.assertIn("ui->bothRadioButton->hide()", calibration_source)
        self.assertIn("Schema 1 Model FormulaLoudnessV1", gui_source)

    @unittest.skipUnless(
        GUI_FACTORY_PATH.is_file() and LEGACY_GUI_PATH.is_file(),
        "editor migration slice has not been added yet",
    )
    def test_legacy_v2_conversion_is_explicit_and_loss_is_disclosed(self) -> None:
        gui_factory_source = GUI_FACTORY_PATH.read_text(encoding="utf-8")
        legacy_gui_source = LEGACY_GUI_PATH.read_text(encoding="utf-8")
        self.assertIn('"(?:^|\\\\s)Version\\\\s+3(?=\\\\s|$)"', gui_factory_source)
        self.assertIn(
            '"(?:^|\\\\s)Model\\\\s+GenericLoudnessV1(?=\\\\s|$)"',
            gui_factory_source,
        )
        self.assertIn('"NeutralVolumeDb", -160.0, 0.0', gui_factory_source)
        self.assertIn('"ManualVolumeDb", -160.0, 0.0', gui_factory_source)
        self.assertIn("parseUnmarkedParameters", gui_factory_source)
        self.assertIn("output.referenceLevel - output.referenceOffset", gui_factory_source)
        self.assertIn("canKeepFormula", gui_factory_source)
        self.assertIn("original shelf-based profile", legacy_gui_source)
        self.assertIn("could be an original shelf profile", legacy_gui_source)
        self.assertIn("Keep existing formula values", legacy_gui_source)
        self.assertIn("Convert original shelf profile", legacy_gui_source)
        self.assertIn("Schema 1 Model FormulaLoudnessV1", legacy_gui_source)
        self.assertIn("values below -100 dB will be clamped", legacy_gui_source)
        self.assertIn("retired headroom mode has no direct equivalent", legacy_gui_source)
        self.assertIn("(std::max)(-100.0", legacy_gui_source)

    @unittest.skipUnless(
        all(path.is_file() for path in RELEASE_CONTRACT_PATHS),
        "release and documentation slice has not been added yet",
    )
    def test_release_version_is_consistent_and_has_no_named_standard_claim(self) -> None:
        version_text = (ROOT / "version.h").read_text(encoding="utf-8")
        parts = {
            name: value
            for name, value in re.findall(
                r"#define\s+(MAJOR|MINOR|REVISION)\s+(\d+)", version_text
            )
        }
        version = ".".join(parts[name] for name in ("MAJOR", "MINOR", "REVISION"))
        manifest = json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8"))
        release_workflow = (
            ROOT / ".github" / "workflows" / "release.yml"
        ).read_text(encoding="utf-8")
        self.assertEqual(version, "3.0.2")
        self.assertEqual(manifest["version-string"], version)
        self.assertIn(f'default: "v{version}"', release_workflow)

        public_text = "\n".join(
            (ROOT / name).read_text(encoding="utf-8")
            for name in ("README.md", "README_zh-TW.md", "NOTICE.md", "CHANGELOG.md")
        )
        self.assertNotRegex(public_text, re.compile(r"ISO\s*-?\s*226", re.IGNORECASE))


if __name__ == "__main__":
    unittest.main()
