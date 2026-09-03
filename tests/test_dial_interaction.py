#!/usr/bin/env python3
"""Regression contracts for the shared circular dial interaction."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class DialInteractionTests(unittest.TestCase):
    def test_custom_paint_uses_value_direction_not_widget_orientation(self) -> None:
        style = read("Editor/CustomStyle.cpp")

        self.assertIn("slider->invertedAppearance()", style)
        self.assertIn("invertedAppearance);", style)
        self.assertNotIn("dial->upsideDown);", style)
        self.assertIn("225.0 - 270.0 * progress", style)

    def test_all_editor_dials_use_relative_vertical_dragging(self) -> None:
        forms = (
            "Editor/guis/BiQuadFilterGUI.ui",
            "Editor/guis/DelayFilterGUI.ui",
            "Editor/guis/PreampFilterGUI.ui",
            "Editor/guis/LoudnessCorrectionFilterGUI.ui",
        )
        for relative_path in forms:
            form = read(relative_path)
            self.assertIn('class="VerticalDragDial"', form, relative_path)
            self.assertNotIn('class="QDial"', form, relative_path)
            self.assertIn("widgets/VerticalDragDial.h", form, relative_path)

    def test_vertical_drag_direction_and_precision_are_explicit(self) -> None:
        source = read("Editor/widgets/VerticalDragDial.cpp")

        self.assertIn("upwardPixels = lastGlobalY - currentGlobalY", source)
        self.assertIn("dragValue += upwardPixels * range", source)
        self.assertIn("Qt::ShiftModifier", source)
        self.assertIn("setSliderDown(true)", source)
        self.assertIn("if (!hasTracking())", source)
        self.assertIn("setValue(sliderPosition())", source)
        self.assertIn("setSliderDown(false)", source)


if __name__ == "__main__":
    unittest.main()
