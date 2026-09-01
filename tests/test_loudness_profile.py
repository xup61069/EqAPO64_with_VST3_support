#!/usr/bin/env python3
"""Regression tests for the data-driven loudness-correction profile."""

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
PHON_LEVELS = tuple(range(0, 101, 10))
REFERENCE_INDEX = 17
FILTER_Q = 3.0
SAMPLE_RATE = 48_000.0


def load_csv() -> list[tuple[float, float, list[float]]]:
	with CSV_PATH.open(newline="", encoding="utf-8") as handle:
		rows = list(csv.DictReader(handle))
	return [
		(
			float(row["freq_Hz"]),
			float(row["hearing_threshold_dB"]),
			[float(row[f"spl_{phon}phon_dB"]) for phon in PHON_LEVELS],
		)
		for row in rows
	]


def load_header_table() -> list[tuple[float, float, list[float]]]:
	text = HEADER_PATH.read_text(encoding="utf-8")
	pattern = re.compile(
		r"\{\s*([0-9]+(?:\.[0-9]+)?),\s*"
		r"(-?[0-9]+(?:\.[0-9]+)?),\s*"
		r"\{([^{}]+)\}\s*\}"
	)
	result: list[tuple[float, float, list[float]]] = []
	for match in pattern.finditer(text):
		values = [float(value.strip()) for value in match.group(3).split(",")]
		if len(values) == len(PHON_LEVELS):
			result.append((float(match.group(1)), float(match.group(2)), values))
	return result


def interpolate(values: list[float], phon: float) -> float:
	phon = min(100.0, max(0.0, phon))
	if phon >= 100.0:
		return values[-1]
	position = phon / 10.0
	lower = int(math.floor(position))
	fraction = position - lower
	return values[lower] + fraction * (values[lower + 1] - values[lower])


def contour_delta(
	table: list[tuple[float, float, list[float]]],
	phon: float,
	reference: float,
	index: int,
) -> float:
	current = interpolate(table[index][2], phon) - interpolate(
		table[REFERENCE_INDEX][2], phon
	)
	baseline = interpolate(table[index][2], reference) - interpolate(
		table[REFERENCE_INDEX][2], reference
	)
	return current - baseline


def peaking_response_db(center: float, gain: float, frequency: float) -> float:
	omega0 = 2.0 * math.pi * center / SAMPLE_RATE
	a = 10.0 ** (max(-48.0, min(48.0, gain)) / 40.0)
	alpha = math.sin(omega0) / (2.0 * FILTER_Q)
	cosine = math.cos(omega0)
	b0 = 1.0 + alpha * a
	b1 = -2.0 * cosine
	b2 = 1.0 - alpha * a
	a0 = 1.0 + alpha / a
	a1 = -2.0 * cosine
	a2 = 1.0 - alpha / a
	z = cmath.exp(-2j * math.pi * frequency / SAMPLE_RATE)
	response = (b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z)
	return 20.0 * math.log10(max(abs(response), 1.0e-15))


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
		cls.csv_table = load_csv()
		cls.header_table = load_header_table()
		frequencies = [row[0] for row in cls.csv_table]
		unit_response = [
			[peaking_response_db(center, 1.0, frequency) for center in frequencies]
			for frequency in frequencies
		]
		cls.response_inverse = invert(unit_response)

	def test_embedded_table_exactly_matches_csv(self) -> None:
		self.assertEqual(len(self.csv_table), 29)
		self.assertEqual(self.header_table, self.csv_table)

	def test_one_kilohertz_row_is_phon_reference(self) -> None:
		self.assertEqual(self.csv_table[REFERENCE_INDEX][0], 1000.0)
		self.assertEqual(self.csv_table[REFERENCE_INDEX][2], list(PHON_LEVELS))
		for phon in range(101):
			self.assertAlmostEqual(
				contour_delta(self.csv_table, phon, 80.0, REFERENCE_INDEX),
				0.0,
				places=12,
			)

	def test_loudness_formula_has_exact_one_kilohertz_identity(self) -> None:
		# alpha=0.3, L_U=0 and T_f=2.4 make the two terms collapse to
		# 10**(0.03*L_N), so the supplied equation returns L_N exactly.
		for phon in PHON_LEVELS:
			argument = (4.0e-10) ** (0.3 - 0.3) * (
				10.0 ** (0.03 * phon) - 10.0**0.072
			) + 10.0 ** (0.3 * 2.4 / 10.0)
			result = (10.0 / 0.3) * math.log10(argument)
			self.assertAlmostEqual(result, phon, places=10)

	def test_four_pass_filter_fit_matches_all_csv_anchors(self) -> None:
		frequencies = [row[0] for row in self.csv_table]
		maximum_error = 0.0
		for phon in PHON_LEVELS:
			target = [
				contour_delta(self.csv_table, float(phon), 80.0, index)
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

	def test_headroom_normalization_prevents_positive_gain(self) -> None:
		frequencies = [row[0] for row in self.csv_table]
		scan = [
			20.0 * (20_000.0 / 20.0) ** (index / 255.0)
			for index in range(256)
		]
		for phon in PHON_LEVELS:
			target = [
				contour_delta(self.csv_table, float(phon), 80.0, index)
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
				correction = multiply(
					self.response_inverse,
					[wanted - measured for wanted, measured in zip(target, actual)],
				)
				gains = [
					max(-48.0, min(48.0, gain + change))
					for gain, change in zip(gains, correction)
				]

			maximum = max(
				sum(
					peaking_response_db(center, gain, frequency)
					for center, gain in zip(frequencies, gains)
				)
				for frequency in scan
			)
			headroom_db = max(0.0, maximum) + (0.1 if maximum > 0.0 else 0.0)
			self.assertLessEqual(maximum - headroom_db, 1.0e-12)

	def test_previous_release_entries_open_as_disabled_drafts(self) -> None:
		filter_header = FILTER_HEADER_PATH.read_text(encoding="utf-8")
		self.assertIn("NeutralVolumeDb", filter_header)
		self.assertIn("state = false;", filter_header)
		self.assertIn("return false;", filter_header)


if __name__ == "__main__":
	unittest.main()
