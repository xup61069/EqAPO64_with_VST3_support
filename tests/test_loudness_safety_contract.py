#!/usr/bin/env python3
"""Regression contracts for endpoint binding and safe loudness updates."""

from __future__ import annotations

import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
FILTER_ENGINE = (ROOT / "FilterEngine.cpp").read_text(encoding="utf-8")
FILTER_ENGINE_HEADER = (ROOT / "FilterEngine.h").read_text(encoding="utf-8")
EQUALIZER_APO_SOURCE = (
    ROOT / "EqualizerAPO" / "EqualizerAPO.cpp"
).read_text(encoding="utf-8")
ANALYSIS_THREAD_SOURCE = (
    ROOT / "Editor" / "AnalysisThread.cpp"
).read_text(encoding="utf-8")
FILTER_INTERFACE = (ROOT / "IFilter.h").read_text(encoding="utf-8")
FILTER_HEADER = (
    ROOT / "filters" / "loudnessCorrection" / "LoudnessCorrectionFilter.h"
).read_text(encoding="utf-8")
PARAMETER_ARCHIVE_HEADER = (
    ROOT / "filters" / "loudnessCorrection" / "ParameterArchive.h"
).read_text(encoding="utf-8")
FILTER_SOURCE = (
    ROOT / "filters" / "loudnessCorrection" / "LoudnessCorrectionFilter.cpp"
).read_text(encoding="utf-8")
VOLUME_SOURCE = (
    ROOT / "filters" / "loudnessCorrection" / "VolumeController.cpp"
).read_text(encoding="utf-8")
VOLUME_HEADER = (
    ROOT / "filters" / "loudnessCorrection" / "VolumeController.h"
).read_text(encoding="utf-8")
BENCHMARK_SOURCE = (ROOT / "Benchmark" / "Benchmark.cpp").read_text(
    encoding="utf-8"
)
RUNTIME_TEST_SOURCE = (
    ROOT / "scripts" / "test-runtime-loudness.ps1"
).read_text(encoding="utf-8")
GUI_PATH = ROOT / "Editor" / "guis" / "LoudnessCorrectionFilterGUI.cpp"
GUI_UI_PATH = ROOT / "Editor" / "guis" / "LoudnessCorrectionFilterGUI.ui"
CALIBRATION_PATH = (
    ROOT / "Editor" / "guis" / "LoudnessCorrectionFilterGUIDialog.cpp"
)
GUI_FACTORY_PATH = (
    ROOT / "Editor" / "guis" / "LoudnessCorrectionFilterGUIFactory.cpp"
)
GUI_FACTORY_HEADER_PATH = (
    ROOT / "Editor" / "guis" / "LoudnessCorrectionFilterGUIFactory.h"
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
PUBLIC_RELEASE_TEXT_PATHS = tuple(
    ROOT / name
    for name in (
        "README.md",
        "README_zh-TW.md",
        "NOTICE.md",
        "CHANGELOG.md",
        "CONTRIBUTING.md",
        "SECURITY.md",
        "Release checklist.txt",
        ".github/PULL_REQUEST_TEMPLATE.md",
        ".github/ISSUE_TEMPLATE/bug_report.md",
        ".github/workflows/build.yml",
        ".github/workflows/release.yml",
    )
)


class LoudnessSafetyContractTests(unittest.TestCase):
    def test_formula_parameters_are_versioned_and_legacy_values_fail_closed(self) -> None:
        self.assertIn('archive.add(1, L"Schema")', FILTER_HEADER)
        self.assertIn('L"FormulaLoudnessV1"', FILTER_HEADER)
        self.assertIn("if (!(isFormulaSchema && isFormulaModel))", FILTER_HEADER)
        self.assertIn("if (referenceLevel <= 0.0f)", FILTER_HEADER)
        self.assertNotIn("referenceLevel = 80.0f;", FILTER_HEADER)

    def test_wide_literal_field_names_round_trip_through_native_codec(self) -> None:
        self.assertIn(
            "to_WString_type_traits<wchar_t[size]>", PARAMETER_ARCHIVE_HEADER
        )
        self.assertIn("return std::wstring(input);", PARAMETER_ARCHIVE_HEADER)
        self.assertIn(
            '"Schema 1 Model FormulaLoudnessV1 Binding All ", 0',
            BENCHMARK_SOURCE,
        )
        self.assertIn('checkCase("marked-round-trip"', BENCHMARK_SOURCE)
        self.assertIn(
            'checkCase("unmarked-formula-fails-closed"', BENCHMARK_SOURCE
        )
        self.assertIn(
            'checkCase("unmarked-mixomo-legacy-fails-closed"', BENCHMARK_SOURCE
        )

    def test_workflow_runtime_configs_are_marked_and_legacy_stays_bypassed(self) -> None:
        marker = "Schema 1 Model FormulaLoudnessV1"
        runtime_configs = re.findall(
            r'"(LoudnessCorrection:[^"\r\n]+)"', RUNTIME_TEST_SOURCE
        )
        marked_configs = [config for config in runtime_configs if marker in config]
        unmarked_configs = [config for config in runtime_configs if marker not in config]

        self.assertGreaterEqual(len(marked_configs), 4)
        self.assertTrue(all("State " in config for config in marked_configs))
        self.assertTrue(all("Binding Single" in config for config in marked_configs))
        self.assertEqual(
            unmarked_configs,
            [
                "LoudnessCorrection: State 1 ReferenceLevel 80 "
                "ReferenceOffset 0 Attenuation 1.0 Volume -38.0"
            ],
        )
        self.assertIn('$legacyConfig = Join-Path', RUNTIME_TEST_SOURCE)
        self.assertIn("Legacy fail-closed benchmark failed", RUNTIME_TEST_SOURCE)
        self.assertIn("if ($legacyHash -ne $disabledHash)", RUNTIME_TEST_SOURCE)
        self.assertIn(
            "Unmarked legacy loudness settings did not fail closed to bypass",
            RUNTIME_TEST_SOURCE,
        )

        for workflow_name in ("build.yml", "release.yml"):
            workflow = (
                ROOT / ".github" / "workflows" / workflow_name
            ).read_text(encoding="utf-8")
            self.assertIn(r"scripts\test-runtime-loudness.ps1", workflow)

    def test_runtime_context_is_applied_before_filter_initialization(self) -> None:
        context_index = FILTER_ENGINE.index("filter->setRuntimeContext(runtimeContext)")
        initialize_index = FILTER_ENGINE.index("filter->initialize(")
        self.assertLess(context_index, initialize_index)
        self.assertIn("bool flowKnown = false;", FILTER_INTERFACE)
        self.assertIn("deviceInfoKnown(false)", FILTER_ENGINE)
        self.assertIn("this->deviceInfoKnown = true;", FILTER_ENGINE)
        self.assertIn("runtimeContext.flowKnown = deviceInfoKnown;", FILTER_ENGINE)
        self.assertIn("runtimeContext.endpointId = deviceGuid;", FILTER_ENGINE)
        self.assertNotIn('L"{0.0.0.00000000}."', FILTER_ENGINE)
        self.assertNotIn('L"{0.0.1.00000000}."', FILTER_ENGINE)

    def test_editor_analysis_explicitly_bypasses_realtime_cold_start(self) -> None:
        self.assertIn("bool offlineAnalysis = false;", FILTER_INTERFACE)
        self.assertIn("void setOfflineAnalysis(bool offlineAnalysis);", FILTER_ENGINE_HEADER)
        self.assertIn("runtimeContext.offlineAnalysis = offlineAnalysis;", FILTER_ENGINE)
        self.assertNotIn("setOfflineAnalysis", EQUALIZER_APO_SOURCE)
        self.assertLess(
            ANALYSIS_THREAD_SOURCE.index("engine.setOfflineAnalysis(true);"),
            ANALYSIS_THREAD_SOURCE.index("engine.initialize("),
        )
        self.assertIn("_runtimeContext.offlineAnalysis", FILTER_SOURCE)
        polling_block = FILTER_SOURCE.split(
            "// Manual mode is immutable for the lifetime of a filter instance", 1
        )[1].split("return channelNames;", 1)[0]
        self.assertIn("!_runtimeContext.offlineAnalysis", polling_block)
        self.assertIn("runLoudnessOfflineAnalysisTests", BENCHMARK_SOURCE)

    def test_apo_reuse_clears_stale_device_identity_before_lookup(self) -> None:
        self.assertIn("void clearDeviceInfo();", FILTER_ENGINE_HEADER)
        clear_body = FILTER_ENGINE.split(
            "void FilterEngine::clearDeviceInfo()", maxsplit=1
        )[1].split("void FilterEngine::setDeviceInfo", maxsplit=1)[0]
        for reset in (
            "deviceInfoKnown = false;",
            "capture = false;",
            "postMixInstalled = true;",
            "deviceName.clear();",
            "connectionName.clear();",
            "deviceGuid.clear();",
            "deviceString.clear();",
        ):
            self.assertIn(reset, clear_body)
        initialize_body = EQUALIZER_APO_SOURCE.split(
            "HRESULT EqualizerAPO::Initialize", maxsplit=1
        )[1].split("HRESULT EqualizerAPO::IsInputFormatSupported", maxsplit=1)[0]
        self.assertLess(
            initialize_body.index("engine.clearDeviceInfo();"),
            initialize_body.index("DeviceAPOInfo apoInfo;"),
        )
        self.assertIn("runFilterEngineDeviceInfoReuseTests", BENCHMARK_SOURCE)

    def test_volume_binding_is_explicit_and_never_uses_an_implicit_fallback(self) -> None:
        self.assertIn('archive.add(std::wstring(', FILTER_HEADER)
        self.assertIn('L"All" : L"Single"', FILTER_HEADER)
        self.assertIn("bindingCount > 1", FILTER_HEADER)
        self.assertIn("binding = BINDING_SINGLE", FILTER_HEADER)
        self.assertIn(
            "deviceEnumerator->GetDevice(_requestedEndpointId.c_str(), &device)",
            VOLUME_SOURCE,
        )
        self.assertIn("if (!_requestedEndpointId.empty())", VOLUME_SOURCE)
        self.assertIn(
            "GetDefaultAudioEndpoint(eRender, eMultimedia, &device)",
            VOLUME_SOURCE,
        )
        self.assertIn(
            "if (_parameters.binding == FilterParameters::BINDING_ALL)",
            FILTER_SOURCE,
        )
        self.assertIn("return L\"\";", FILTER_SOURCE)
        self.assertIn("_runtimeContext.isCapture", FILTER_SOURCE)
        self.assertIn("EnumAudioEndpoints(", VOLUME_SOURCE)
        self.assertIn("ENDPOINT_GUID_PROPERTY", VOLUME_SOURCE)
        self.assertIn("candidate->OpenPropertyStore(", VOLUME_SOURCE)
        self.assertIn("device->GetId(&endpointId)", VOLUME_SOURCE)
        self.assertNotIn("waveOutGetVolume", VOLUME_SOURCE)
        self.assertNotIn("GetMasterVolumeLevelScalar", VOLUME_SOURCE)

    def test_global_default_rebind_failure_clears_the_old_endpoint(self) -> None:
        self.assertIn("bool refreshEndpointIfChanged();", VOLUME_HEADER)
        refresh_body = VOLUME_SOURCE.split(
            "bool VolumeController::refreshEndpointIfChanged()", maxsplit=1
        )[1].split("HRESULT VolumeController::getVolume", maxsplit=1)[0]
        self.assertGreaterEqual(refresh_body.count("cleanup();"), 4)
        changed_body = refresh_body.split("if (changed)", maxsplit=1)[1]
        self.assertLess(changed_body.index("cleanup();"), changed_body.index("initEndpoint()"))
        get_body = VOLUME_SOURCE.split(
            "HRESULT VolumeController::getVolume", maxsplit=1
        )[1].split("HRESULT VolumeController::setVolume", maxsplit=1)[0]
        self.assertIn("if (!refreshEndpointIfChanged())", get_body)
        self.assertIn("return E_FAIL;", get_body)
        failed_read = get_body.split("if (FAILED(res))", maxsplit=1)[1]
        self.assertLess(failed_read.index("cleanup();"), failed_read.index("initEndpoint()"))

    def test_global_binding_uses_windows_default_without_apo_metadata(self) -> None:
        can_track_body = FILTER_SOURCE.split(
            "bool LoudnessCorrectionFilter::canTrackAutomaticVolume() const",
            maxsplit=1,
        )[1].split(
            "std::wstring LoudnessCorrectionFilter::getVolumeControllerEndpointId() const",
            maxsplit=1,
        )[0]
        global_binding = can_track_body.index(
            "if (_parameters.binding == FilterParameters::BINDING_ALL)"
        )
        runtime_context = can_track_body.index(
            "if (!_runtimeContext.flowKnown || _runtimeContext.isCapture)"
        )
        self.assertLess(global_binding, runtime_context)
        self.assertIn("return true;", can_track_body[global_binding:runtime_context])
        self.assertIn("_runtimeContext.isCapture", FILTER_SOURCE)
        self.assertGreaterEqual(
            FILTER_SOURCE.count("_runtimeBypass.store(true"),
            3,
        )
        self.assertIn("_runtimeBypass.load(std::memory_order_acquire)", FILTER_SOURCE)
        self.assertIn(
            "VolumeController volumeController(self->getVolumeControllerEndpointId())",
            FILTER_SOURCE,
        )
        self.assertIn(
            "could not create the endpoint-volume tracking event",
            FILTER_SOURCE,
        )
        self.assertIn("_recoveryPending", FILTER_HEADER)
        self.assertIn("_transitionFromBypass", FILTER_HEADER)
        self.assertIn("if (!recovering && std::isfinite(lastVolume)", FILTER_SOURCE)
        self.assertIn("_hasInitialAutomaticVolume = true;", FILTER_SOURCE)
        self.assertIn("self->_hasInitialAutomaticVolume ?", FILTER_SOURCE)
        self.assertIn("self->_initialAutomaticVolume", FILTER_SOURCE)
        self.assertIn(
            "crossfade from the common magnitude-unity A = L + H domain",
            FILTER_SOURCE,
        )

    def test_dynamic_coefficients_use_preallocated_crossfade_banks(self) -> None:
        self.assertIn("_biquadBanks[2]", FILTER_HEADER)
        self.assertIn("_lowpassBanks[2]", FILTER_HEADER)
        self.assertIn("_highpassBanks[2]", FILTER_HEADER)
        self.assertIn("CROSSOVER_SECTION_COUNT = 14", FILTER_HEADER)
        self.assertIn("CROSSOVER_BUTTERWORTH_ORDER = 14", FILTER_HEADER)
        self.assertIn("CROSSOVER_HISTORY_PREWARM_SECONDS = 1.0", FILTER_HEADER)
        self.assertIn("BYPASS_FADE_SECONDS = 0.01", FILTER_HEADER)
        self.assertIn("FILTER_WARMUP_SECONDS = 0.25", FILTER_HEADER)
        self.assertIn("COEFFICIENT_CROSSFADE_SECONDS = 0.1", FILTER_HEADER)
        self.assertIn("FINAL_RESPONSE_NUMERICAL_TOLERANCE_DB = 1.0e-6", FILTER_HEADER)
        self.assertIn("TryEnterCriticalSection", FILTER_SOURCE)
        self.assertIn("resetState()", FILTER_SOURCE)
        self.assertIn("_warmupActive", FILTER_SOURCE)
        self.assertIn("_crossfadeActive", FILTER_SOURCE)
        self.assertIn("highpassIdentitySample * outputGainLinear", FILTER_SOURCE)
        self.assertIn("lowpass + highpass * correction", FILTER_SOURCE)
        self.assertIn("_crossoverPrewarmActive", FILTER_SOURCE)
        self.assertIn("isSafeCrossoverHandoff", FILTER_SOURCE)
        self.assertIn("handoffStep <= naturalStep + tolerance", FILTER_SOURCE)
        self.assertNotIn("CROSSOVER_HANDOFF_BRIDGE_SAMPLES", FILTER_HEADER)
        self.assertIn("_crossoverDomainActive", FILTER_HEADER)
        self.assertIn("!_bypassFadeActive", FILTER_SOURCE)
        self.assertIn(
            "residualGain * (audibleComposite - activeSample.identity)",
            FILTER_SOURCE,
        )
        self.assertIn("_coeffsUpdated.store(!initialIdentity", FILTER_SOURCE)
        runtime_bypass_body = FILTER_SOURCE.split(
            "else if (runtimeBypass && !crossoverDomainReady)", maxsplit=1
        )[1].split("bool recoveryReady", maxsplit=1)[0]
        self.assertNotIn("_crossoverPrewarmActive = false", runtime_bypass_body)
        self.assertIn("_lowpassBanks[_transitionBankIndex][channel][section] =", FILTER_SOURCE)
        self.assertIn("_highpassBanks[_transitionBankIndex][channel][section] =", FILTER_SOURCE)
        process_body = FILTER_SOURCE.split(
            "void LoudnessCorrectionFilter::process", maxsplit=1
        )[1]
        self.assertNotIn(".resize(", process_body)
        self.assertNotIn(".push_back(", process_body)
        self.assertNotIn("new ", process_body)
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
        self.assertIn("runLoudnessCrossoverCoefficientTests", BENCHMARK_SOURCE)
        self.assertIn("runLoudnessRuntimeContextTests", BENCHMARK_SOURCE)
        self.assertIn("runLoudnessSubsonicSineCase", BENCHMARK_SOURCE)
        self.assertIn("runLoudnessAdaptiveHandoffSweep", BENCHMARK_SOURCE)
        self.assertIn("runLoudnessExactHandoffCase", BENCHMARK_SOURCE)
        self.assertIn("runLoudnessCommonDomainCase", BENCHMARK_SOURCE)
        self.assertIn("runLoudnessPartialHandoffFailureCase", BENCHMARK_SOURCE)
        self.assertIn("FAILURE_AT_WARMUP", BENCHMARK_SOURCE)
        self.assertIn("FAILURE_AT_CROSSFADE", BENCHMARK_SOURCE)
        self.assertIn("12.7711", BENCHMARK_SOURCE)
        self.assertIn("maximumSecondDifferenceRatio", BENCHMARK_SOURCE)
        self.assertIn("bypassFadeStartCount", BENCHMARK_SOURCE)
        self.assertIn("--loudness-transition-test", RUNTIME_TEST_SOURCE)

    @unittest.skipUnless(
        GUI_PATH.is_file() and CALIBRATION_PATH.is_file() and LEGACY_GUI_PATH.is_file(),
        "editor safety slice has not been added yet",
    )
    def test_editor_requires_readable_endpoint_and_rechecks_calibration(self) -> None:
        gui_source = GUI_PATH.read_text(encoding="utf-8")
        gui_ui = GUI_UI_PATH.read_text(encoding="utf-8")
        calibration_source = CALIBRATION_PATH.read_text(encoding="utf-8")
        self.assertIn("FAILED(volumeController->getVolume(endpointVolume))", gui_source)
        self.assertIn('tr("Manual volume (required):")', gui_source)
        self.assertIn('name="bindingComboBox"', gui_ui)
        self.assertIn(
            "Single endpoint", gui_ui
        )
        self.assertIn(
            "Global (Windows default)",
            gui_ui,
        )
        self.assertIn(
            "use Global only when that master volume is the intended shared control",
            gui_ui,
        )
        self.assertIn("If a Matrix endpoint is muted or fixed", gui_ui)
        self.assertIn("new VolumeController(requestedEndpointId)", gui_source)
        self.assertIn("endpointId = volumeController->getEndpointId()", gui_source)
        refresh_body = gui_source.split(
            "void LoudnessCorrectionFilterGUI::refreshVolumeController()",
            maxsplit=1,
        )[1].split(
            "void LoudnessCorrectionFilterGUI::updateAutomaticVolumeUi()",
            maxsplit=1,
        )[0]
        self.assertRegex(
            refresh_body,
            r"if \(getBindingMode\(\) ==\s+"
            r"LoudnessCorrectionFilter::FilterParameters::BINDING_SINGLE &&\s+"
            r"!selectedEndpointIsRender\)",
        )
        play_handler = calibration_source.split(
            "void LoudnessCorrectionFilterGUIDialog::on_playButton_clicked()",
            maxsplit=1,
        )[1]
        self.assertLess(
            play_handler.index("isPlaybackEndpointStillValid()"),
            play_handler.index("PlaySoundA("),
        )
        playback_body = play_handler.split(
            "void LoudnessCorrectionFilterGUIDialog::on_stopButton_clicked()",
            maxsplit=1,
        )[0]
        first_check = playback_body.index("isPlaybackEndpointStillValid()")
        second_check = playback_body.index(
            "isPlaybackEndpointStillValid()", first_check + 1
        )
        decode_complete = playback_body.index("buffer.close();")
        play_sound = playback_body.index("PlaySoundA(buffer.data().data()")
        self.assertLess(decode_complete, second_check)
        self.assertLess(second_check, play_sound)
        self.assertIn("GetDefaultAudioEndpoint(eRender, role", calibration_source)
        self.assertIn("isDefaultRenderEndpoint(endpointId, eConsole)", calibration_source)
        self.assertIn("isDefaultRenderEndpoint(endpointId, eMultimedia)", calibration_source)
        self.assertIn("endpointGuardTimer.start(250)", calibration_source)
        self.assertIn("&QTimer::timeout", calibration_source)
        self.assertIn("&& !tryUpdateVolume())", gui_source)
        self.assertIn('tr("Calibration not applied")', gui_source)
        self.assertIn("ui->bothRadioButton->hide()", calibration_source)
        self.assertIn("Schema 1 Model FormulaLoudnessV1", gui_source)

        gui_factory_source = GUI_FACTORY_PATH.read_text(encoding="utf-8")
        gui_factory_header = GUI_FACTORY_HEADER_PATH.read_text(encoding="utf-8")
        self.assertIn(
            "bool getSelectedRenderEndpoint(std::wstring& endpointId) const;",
            gui_factory_header,
        )
        selected_render_body = gui_factory_source.split(
            "bool LoudnessCorrectionFilterGUIFactory::getSelectedRenderEndpoint(",
            maxsplit=1,
        )[1]
        self.assertIn("endpointId = selectedDevice->getDeviceGuid();", selected_render_body)
        self.assertIn("return true;", selected_render_body)
        self.assertNotIn("if (deviceGuid.empty())", selected_render_body)
        self.assertIn("selectedEndpointIsRender", gui_factory_source)
        self.assertIn("selectedEndpointIsRender && !endpointId.empty()", gui_factory_source)

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
        self.assertIn("FormulaLoudnessV1 Binding Single", legacy_gui_source)
        self.assertIn("FormulaLoudnessV1 Binding All", legacy_gui_source)
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
        self.assertEqual(version, "3.0.5")
        self.assertEqual(manifest["version-string"], version)
        self.assertIn(f'default: "v{version}"', release_workflow)

        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        first_changelog_version = re.search(r"^##\s+(\d+\.\d+\.\d+)\s*$", changelog, re.MULTILINE)
        self.assertIsNotNone(first_changelog_version)
        self.assertEqual(first_changelog_version.group(1), version)

        public_text = "\n".join(
            path.read_text(encoding="utf-8") for path in PUBLIC_RELEASE_TEXT_PATHS
        )
        forbidden_claims = (
            r"\bISO\s*-?\s*226\b",
            r"\bstandards?[- ]?(?:compliant|conformant)\b",
            r"\bcertified\s+(?:to|under)\b",
        )
        for pattern in forbidden_claims:
            self.assertNotRegex(public_text, re.compile(pattern, re.IGNORECASE))

        marker = "Schema 1 Model FormulaLoudnessV1"
        english_readme = (ROOT / "README.md").read_text(encoding="utf-8")
        chinese_readme = (ROOT / "README_zh-TW.md").read_text(encoding="utf-8")
        self.assertGreaterEqual(english_readme.count(marker), 3)
        self.assertGreaterEqual(chinese_readme.count(marker), 3)
        fork_url = "https://github.com/Mixomo/EqAPO64_with_VST3_support"
        self.assertIn(fork_url, english_readme)
        self.assertIn(fork_url, chinese_readme)


if __name__ == "__main__":
    unittest.main()
