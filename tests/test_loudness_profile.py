#!/usr/bin/env python3
"""Regression tests for the formula-driven loudness-correction profile."""

from __future__ import annotations

import cmath
import csv
import math
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
CSV_PATH = ROOT / "filters" / "loudnessCorrection" / "loudness_profile.csv"
HEADER_PATH = ROOT / "filters" / "loudnessCorrection" / "LoudnessProfile.h"
FILTER_HEADER_PATH = ROOT / "filters" / "loudnessCorrection" / "LoudnessCorrectionFilter.h"
FILTER_CPP_PATH = ROOT / "filters" / "loudnessCorrection" / "LoudnessCorrectionFilter.cpp"
GUI_FACTORY_PATH = ROOT / "Editor" / "guis" / "LoudnessCorrectionFilterGUIFactory.cpp"
LEGACY_GUI_PATH = ROOT / "Editor" / "guis" / "LegacyLoudnessCorrectionFilterGUI.cpp"
PHON_LEVELS = tuple(range(0, 101, 10))
REFERENCE_INDEX = 17
FILTER_Q = 3.0
SAMPLE_RATE = 48_000.0
SUBSONIC_CROSSOVER_HZ = 25.0
CROSSOVER_BUTTERWORTH_ORDER = 14
CROSSOVER_SECTION_COUNT = 14

EXPECTED_PARAMETERS = [
	(20.0, 0.635, -31.5, 78.1),
	(25.0, 0.602, -27.2, 68.7),
	(31.5, 0.569, -23.1, 59.5),
	(40.0, 0.537, -19.3, 51.1),
	(50.0, 0.509, -16.1, 44.0),
	(63.0, 0.482, -13.1, 37.5),
	(80.0, 0.456, -10.4, 31.5),
	(100.0, 0.433, -8.2, 26.5),
	(125.0, 0.412, -6.3, 22.1),
	(160.0, 0.391, -4.6, 17.9),
	(200.0, 0.373, -3.2, 14.4),
	(250.0, 0.357, -2.1, 11.4),
	(315.0, 0.343, -1.2, 8.6),
	(400.0, 0.330, -0.5, 6.2),
	(500.0, 0.320, 0.0, 4.4),
	(630.0, 0.311, 0.4, 3.0),
	(800.0, 0.303, 0.5, 2.2),
	(1000.0, 0.300, 0.0, 2.4),
	(1250.0, 0.295, -2.7, 3.5),
	(1600.0, 0.292, -4.2, 1.7),
	(2000.0, 0.290, -1.2, -1.3),
	(2500.0, 0.290, 1.4, -4.2),
	(3150.0, 0.289, 2.3, -6.0),
	(4000.0, 0.289, 1.0, -5.4),
	(5000.0, 0.289, -2.3, -1.5),
	(6300.0, 0.293, -7.2, 6.0),
	(8000.0, 0.303, -11.2, 12.6),
	(10000.0, 0.323, -10.9, 13.9),
	(12500.0, 0.354, -3.5, 12.3),
]


def load_csv() -> list[tuple[float, float, float, float]]:
	with CSV_PATH.open(newline="", encoding="utf-8") as handle:
		rows = list(csv.DictReader(handle))
	return [
		(
			float(row["freq_Hz"]),
			float(row["alpha_f"]),
			float(row["Lu_dB"]),
			float(row["Tf_dB"]),
		)
		for row in rows
	]


def load_header_table() -> list[tuple[float, float, float, float]]:
	text = HEADER_PATH.read_text(encoding="utf-8")
	pattern = re.compile(
		r"\{\s*([0-9]+(?:\.[0-9]+)?),\s*"
		r"([0-9]+(?:\.[0-9]+)?),\s*"
		r"(-?[0-9]+(?:\.[0-9]+)?),\s*"
		r"(-?[0-9]+(?:\.[0-9]+)?)\s*\}"
	)
	result: list[tuple[float, float, float, float]] = []
	for match in pattern.finditer(text):
		result.append(tuple(float(match.group(index)) for index in range(1, 5)))
	return result


def compute_spl(parameters: tuple[float, float, float, float], phon: float) -> float:
	_frequency, alpha, lu, tf = parameters
	argument = (4.0e-10) ** (0.3 - alpha) * (
		10.0 ** (0.03 * phon) - 10.0**0.072
	) + 10.0 ** (alpha * (tf + lu) / 10.0)
	return (10.0 / alpha) * math.log10(argument) - lu


def compute_loudness(parameters: tuple[float, float, float, float], spl: float) -> float:
	_frequency, alpha, lu, tf = parameters
	argument = (
		10.0 ** (alpha * (spl + lu) / 10.0)
		- 10.0 ** (alpha * (tf + lu) / 10.0)
	) / (4.0e-10) ** (0.3 - alpha) + 10.0**0.072
	return (100.0 / 3.0) * math.log10(argument)


def contour_delta(
	parameters: list[tuple[float, float, float, float]],
	phon: float,
	reference: float,
	index: int,
) -> float:
	# Production evaluates the formula directly, including between 10-phon
	# anchors. Test against the same continuous values rather than interpolation.
	current = compute_spl(parameters[index], phon) - compute_spl(
		parameters[REFERENCE_INDEX], phon
	)
	baseline = compute_spl(parameters[index], reference) - compute_spl(
		parameters[REFERENCE_INDEX], reference
	)
	return current - baseline


def peaking_coefficients(
	center: float,
	gain: float,
	sample_rate: float = SAMPLE_RATE,
) -> tuple[float, float, float, float, float]:
	omega0 = 2.0 * math.pi * center / sample_rate
	a = 10.0 ** (max(-48.0, min(48.0, gain)) / 40.0)
	alpha = math.sin(omega0) / (2.0 * FILTER_Q)
	cosine = math.cos(omega0)
	b0 = 1.0 + alpha * a
	b1 = -2.0 * cosine
	b2 = 1.0 - alpha * a
	a0 = 1.0 + alpha / a
	a1 = -2.0 * cosine
	a2 = 1.0 - alpha / a
	return b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0


def biquad_response(
	coefficients: tuple[float, float, float, float, float],
	frequency: float,
	sample_rate: float,
) -> complex:
	b0, b1, b2, a1, a2 = coefficients
	z = cmath.exp(-2j * math.pi * frequency / sample_rate)
	return (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z)


def peaking_response(
	center: float,
	gain: float,
	frequency: float,
	sample_rate: float = SAMPLE_RATE,
) -> complex:
	return biquad_response(
		peaking_coefficients(center, gain, sample_rate),
		frequency,
		sample_rate,
	)


def peaking_response_db(
	center: float,
	gain: float,
	frequency: float,
	sample_rate: float = SAMPLE_RATE,
) -> float:
	response = peaking_response(center, gain, frequency, sample_rate)
	return 20.0 * math.log10(max(abs(response), 1.0e-15))


def crossover_coefficients(
	high_pass: bool,
	section: int,
	sample_rate: float,
) -> tuple[float, float, float, float, float]:
	butterworth_section = section % (CROSSOVER_BUTTERWORTH_ORDER // 2)
	q = 1.0 / (
		2.0
		* math.sin(
			(2.0 * butterworth_section + 1.0)
			* math.pi
			/ (2.0 * CROSSOVER_BUTTERWORTH_ORDER)
		)
	)
	omega = 2.0 * math.pi * SUBSONIC_CROSSOVER_HZ / sample_rate
	alpha = math.sin(omega) / (2.0 * q)
	cosine = math.cos(omega)
	a0 = 1.0 + alpha
	if high_pass:
		b0 = math.cos(0.5 * omega) ** 2 / a0
		b1 = -2.0 * b0
	else:
		b0 = math.sin(0.5 * omega) ** 2 / a0
		b1 = 2.0 * b0
	return b0, b1, b0, -2.0 * cosine / a0, (1.0 - alpha) / a0


def crossover_response(
	high_pass: bool,
	frequency: float,
	sample_rate: float,
) -> complex:
	response = 1.0 + 0.0j
	for section in range(CROSSOVER_SECTION_COUNT):
		response *= biquad_response(
			crossover_coefficients(high_pass, section, sample_rate),
			frequency,
			sample_rate,
		)
	return response


def fit_correction(
	parameters: list[tuple[float, float, float, float]],
	phon: float,
	reference: float,
	sample_rate: float,
) -> tuple[list[float], list[float]]:
	active = [row for row in parameters if row[0] <= 0.45 * sample_rate]
	frequencies = [row[0] for row in active]
	unit_response = [
		[
			peaking_response_db(center, 1.0, frequency, sample_rate)
			for center in frequencies
		]
		for frequency in frequencies
	]
	response_inverse = invert(unit_response)
	target = [
		contour_delta(parameters, phon, reference, index)
		for index in range(len(frequencies))
	]
	gains = multiply(response_inverse, target)
	for _ in range(3):
		actual = [
			sum(
				peaking_response_db(center, gain, frequency, sample_rate)
				for center, gain in zip(frequencies, gains)
			)
			for frequency in frequencies
		]
		residual = [wanted - measured for wanted, measured in zip(target, actual)]
		correction = multiply(response_inverse, residual)
		gains = [
			max(-48.0, min(48.0, gain + change))
			for gain, change in zip(gains, correction)
		]
	return frequencies, gains


def correction_response(
	frequencies: list[float],
	gains: list[float],
	frequency: float,
	sample_rate: float,
) -> complex:
	response = 1.0 + 0.0j
	for center, gain in zip(frequencies, gains):
		response *= peaking_response(center, gain, frequency, sample_rate)
	return response


def refined_maximum_response_db(
	response_at_frequency,
	sample_rate: float,
) -> float:
	point_count = 4097
	log_minimum = math.log(1.0)
	log_maximum = math.log(min(20_000.0, 0.499 * sample_rate))
	log_step = (log_maximum - log_minimum) / (point_count - 1)
	responses = [
		response_at_frequency(math.exp(log_minimum + point * log_step))
		for point in range(point_count)
	]
	maximum = max(0.0, max(responses))
	golden = 0.6180339887498948482
	for point in range(1, point_count - 1):
		if responses[point] < responses[point - 1] or responses[point] < responses[point + 1]:
			continue
		if responses[point] == responses[point - 1] == responses[point + 1]:
			continue
		left = log_minimum + (point - 1) * log_step
		right = log_minimum + (point + 1) * log_step
		inner_left = right - golden * (right - left)
		inner_right = left + golden * (right - left)
		left_response = response_at_frequency(math.exp(inner_left))
		right_response = response_at_frequency(math.exp(inner_right))
		for _ in range(32):
			if left_response < right_response:
				left = inner_left
				inner_left = inner_right
				left_response = right_response
				inner_right = left + golden * (right - left)
				right_response = response_at_frequency(math.exp(inner_right))
			else:
				right = inner_right
				inner_right = inner_left
				right_response = left_response
				inner_left = right - golden * (right - left)
				left_response = response_at_frequency(math.exp(inner_left))
		maximum = max(maximum, left_response, right_response)
	return maximum


def invert(matrix: list[list[float]]) -> list[list[float]]:
	size = len(matrix)
	augmented = [
		row[:] + [1.0 if row_index == column else 0.0 for column in range(size)]
		for row_index, row in enumerate(matrix)
	]
	for column in range(size):
		pivot = max(range(column, size), key=lambda row: abs(augmented[row][column]))
		if abs(augmented[pivot][column]) < 1.0e-12:
			raise AssertionError("response matrix is singular")
		augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
		divisor = augmented[column][column]
		augmented[column] = [value / divisor for value in augmented[column]]
		for row in range(size):
			if row == column:
				continue
			factor = augmented[row][column]
			augmented[row] = [
				value - factor * pivot_value
				for value, pivot_value in zip(augmented[row], augmented[column])
			]
	return [row[size:] for row in augmented]


def multiply(matrix: list[list[float]], vector: list[float]) -> list[float]:
	return [sum(value * vector[index] for index, value in enumerate(row)) for row in matrix]


class LoudnessProfileTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls) -> None:
		cls.csv_parameters = load_csv()
		cls.header_parameters = load_header_table()
		frequencies = [row[0] for row in cls.csv_parameters]
		unit_response = [
			[peaking_response_db(center, 1.0, frequency) for center in frequencies]
			for frequency in frequencies
		]
		cls.response_inverse = invert(unit_response)

	def test_all_parameters_exactly_match_csv_and_header(self) -> None:
		self.assertEqual(len(self.csv_parameters), 29)
		self.assertEqual(self.csv_parameters, EXPECTED_PARAMETERS)
		self.assertEqual(self.header_parameters, EXPECTED_PARAMETERS)

	def test_runtime_path_calls_formula_with_row_parameters(self) -> None:
		text = HEADER_PATH.read_text(encoding="utf-8")
		self.assertRegex(
			text,
			r"return computeSPLFromFormula\(clampedLevel,\s*parameters\.alpha,"
			r"\s*parameters\.Lu, parameters\.Tf\);",
		)

	def test_formula_golden_values(self) -> None:
		golden_values = (
			(0, 20.0, 89.544478418546),
			(7, 80.0, 92.460642396735),
			(28, 80.0, 85.605878244606),
		)
		for index, phon, expected in golden_values:
			self.assertAlmostEqual(
				compute_spl(self.csv_parameters[index], phon),
				expected,
				places=10,
			)

	def test_one_kilohertz_row_is_phon_reference(self) -> None:
		self.assertEqual(self.csv_parameters[REFERENCE_INDEX][0], 1000.0)
		for phon in range(101):
			self.assertAlmostEqual(
				compute_spl(self.csv_parameters[REFERENCE_INDEX], float(phon)),
				float(phon),
				places=10,
			)
			self.assertAlmostEqual(
				contour_delta(self.csv_parameters, phon, 80.0, REFERENCE_INDEX),
				0.0,
				places=12,
			)

	def test_formula_round_trips_all_frequencies_in_supported_range(self) -> None:
		for parameters in self.csv_parameters:
			upper_level = 90 if parameters[0] <= 4000.0 else 80
			for phon in range(20, upper_level + 1):
				spl = compute_spl(parameters, float(phon))
				self.assertTrue(math.isfinite(spl))
				self.assertAlmostEqual(
					compute_loudness(parameters, spl),
					float(phon),
					places=10,
				)

	def test_four_pass_filter_fit_matches_all_csv_anchors(self) -> None:
		frequencies = [row[0] for row in self.csv_parameters]
		maximum_error = 0.0
		for phon in PHON_LEVELS:
			target = [
				contour_delta(self.csv_parameters, float(phon), 80.0, index)
				for index in range(len(frequencies))
			]
			gains = multiply(self.response_inverse, target)
			for _ in range(3):
				actual = [
					sum(
						peaking_response_db(center, gain, frequency)
						for center, gain in zip(frequencies, gains)
					)
					for frequency in frequencies
				]
				residual = [wanted - measured for wanted, measured in zip(target, actual)]
				correction = multiply(self.response_inverse, residual)
				gains = [
					max(-48.0, min(48.0, gain + change))
					for gain, change in zip(gains, correction)
				]

			actual = [
				sum(
					peaking_response_db(center, gain, frequency)
					for center, gain in zip(frequencies, gains)
				)
				for frequency in frequencies
			]
			maximum_error = max(
				maximum_error,
				max(abs(wanted - measured) for wanted, measured in zip(target, actual)),
			)

		self.assertLessEqual(maximum_error, 0.02)

	def test_lr28_coefficients_are_stable_and_complementary(self) -> None:
		for sample_rate in (8_000.0, 44_100.0, 48_000.0, 96_000.0, 192_000.0, 384_000.0):
			maximum_pole_radius = 0.0
			for section in range(CROSSOVER_SECTION_COUNT):
				low = crossover_coefficients(False, section, sample_rate)
				high = crossover_coefficients(True, section, sample_rate)
				for coefficients in (low, high):
					self.assertTrue(all(math.isfinite(value) for value in coefficients))
					_a0, _a1, _a2, denominator_a1, denominator_a2 = coefficients
					discriminant = cmath.sqrt(
						denominator_a1 * denominator_a1 - 4.0 * denominator_a2
					)
					poles = (
						(-denominator_a1 + discriminant) / 2.0,
						(-denominator_a1 - discriminant) / 2.0,
					)
					maximum_pole_radius = max(
						maximum_pole_radius,
						*(abs(pole) for pole in poles),
					)
			self.assertLess(maximum_pole_radius, 1.0)
			for frequency in (1.0, 5.0, 10.0, 15.0, 19.0, 20.0, 25.0, 31.5, 100.0):
				complement = crossover_response(False, frequency, sample_rate) + crossover_response(
					True, frequency, sample_rate
				)
				self.assertLessEqual(abs(abs(complement) - 1.0), 1.0e-6)

	def test_high_rate_raw_anchor_fit_is_unchanged(self) -> None:
		for sample_rate in (48_000.0, 192_000.0):
			frequencies, gains = fit_correction(
				self.csv_parameters, 40.0, 80.0, sample_rate
			)
			maximum_error = 0.0
			for index, frequency in enumerate(frequencies):
				target = contour_delta(self.csv_parameters, 40.0, 80.0, index)
				actual = 20.0 * math.log10(
					max(
						abs(correction_response(frequencies, gains, frequency, sample_rate)),
						1.0e-15,
					)
				)
				maximum_error = max(maximum_error, abs(actual - target))
			self.assertLessEqual(maximum_error, 0.02)

	def test_subsonic_guard_is_near_unity_and_full_transfer_has_headroom(self) -> None:
		for sample_rate in (8_000.0, 48_000.0, 192_000.0, 384_000.0):
			frequencies, gains = fit_correction(
				self.csv_parameters, 0.0, 100.0, sample_rate
			)

			def raw_response_db(frequency: float, output_gain: float) -> float:
				response = output_gain * correction_response(
					frequencies, gains, frequency, sample_rate
				)
				return 20.0 * math.log10(max(abs(response), 1.0e-15))

			raw_peak = refined_maximum_response_db(
				lambda frequency: raw_response_db(frequency, 1.0),
				sample_rate,
			)
			output_gain = (
				1.0 if raw_peak <= 0.0 else 10.0 ** (-(raw_peak + 1.0) / 20.0)
			)

			def guarded_response_db(frequency: float, gain: float) -> float:
				lowpass = crossover_response(False, frequency, sample_rate)
				highpass = crossover_response(True, frequency, sample_rate)
				correction = correction_response(
					frequencies, gains, frequency, sample_rate
				)
				response = lowpass + highpass * gain * correction
				return 20.0 * math.log10(max(abs(response), 1.0e-15))

			full_peak = refined_maximum_response_db(
				lambda frequency: guarded_response_db(frequency, output_gain),
				sample_rate,
			)

			for frequency in (1.0, 5.0, 10.0, 15.0, 19.0):
				self.assertLessEqual(
					abs(guarded_response_db(frequency, output_gain)),
					0.01,
				)
			self.assertLessEqual(full_peak, 1.0e-6)

		filter_cpp = FILTER_CPP_PATH.read_text(encoding="utf-8")
		self.assertIn("lowpass + highpass * correction", filter_cpp)
		self.assertIn("sineHalf * sineHalf", filter_cpp)
		self.assertIn("highpassIdentitySample * outputGainLinear", filter_cpp)

	def test_headroom_uses_dense_refined_peak_search(self) -> None:
		filter_header = FILTER_HEADER_PATH.read_text(encoding="utf-8")
		filter_cpp = FILTER_CPP_PATH.read_text(encoding="utf-8")
		self.assertIn("HEADROOM_MARGIN_DB = 1.0", filter_header)
		self.assertIn("RESPONSE_SCAN_POINTS = 4097", filter_header)
		self.assertIn("RESPONSE_REFINEMENT_ITERATIONS = 32", filter_header)
		self.assertIn("goldenRatioConjugate", filter_cpp)
		self.assertIn("0.499 * static_cast<double>(_sampleRate)", filter_cpp)

	@unittest.skipUnless(
		GUI_FACTORY_PATH.is_file() and LEGACY_GUI_PATH.is_file(),
		"editor migration slice has not been added yet",
	)
	def test_previous_release_requires_explicit_enabled_migration(self) -> None:
		filter_header = FILTER_HEADER_PATH.read_text(encoding="utf-8")
		factory = GUI_FACTORY_PATH.read_text(encoding="utf-8")
		legacy_gui = LEGACY_GUI_PATH.read_text(encoding="utf-8")
		self.assertNotIn("NeutralVolumeDb", filter_header)
		self.assertIn("parseGenericV2Parameters", factory)
		self.assertIn("parseUnmarkedParameters", factory)
		self.assertIn("output.referenceLevel - output.referenceOffset", factory)
		self.assertIn("migration == Migration::None", legacy_gui)
		self.assertIn("parameters = originalParameters", legacy_gui)
		self.assertIn("Migration::KeepFormula", legacy_gui)
		self.assertIn("Migration::ConvertShelf", legacy_gui)
		self.assertIn(
			"Schema 1 Model FormulaLoudnessV1 Binding All State 1 ReferenceLevel 80 ReferenceOffset",
			legacy_gui,
		)


if __name__ == "__main__":
	unittest.main()
