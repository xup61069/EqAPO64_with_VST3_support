#!/usr/bin/env python3
"""Contracts for bounded, still-operable editor surfaces."""

from __future__ import annotations

import pathlib
import unittest
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).resolve().parents[1]
GUIS = ROOT / "Editor" / "guis"


def property_text(widget: ET.Element, name: str) -> str:
    prop = widget.find(f"./property[@name='{name}']")
    if prop is None or len(prop) != 1:
        return ""
    return prop[0].text or ""


class EditorSizeResilienceTests(unittest.TestCase):
    def test_vst_row_omits_the_non_actionable_compatibility_wall(self) -> None:
        source = (GUIS / "VSTPluginFilterGUI.cpp").read_text(encoding="utf-8")
        ui = ET.parse(GUIS / "VSTPluginFilterGUI.ui").getroot()

        self.assertNotIn(
            "NOTE: The VST module is not universally compatible", source
        )
        self.assertNotIn("QLabel* note", source)
        self.assertNotIn("grid->addWidget(note", source)
        self.assertIn("updatePermissionWarning();", source)

        permission_warning = source[
            source.index("void VSTPluginFilterGUI::updatePermissionWarning") :
        ]
        self.assertIn("warningTextEdit->setPlainText", permission_warning)
        self.assertIn("not readable by the audio service", permission_warning)

        geometry_height = ui.findtext(
            ".//widget[@name='VSTPluginFilterGUI']/property[@name='geometry']/rect/height"
        )
        self.assertIsNotNone(geometry_height)
        self.assertLessEqual(int(geometry_height), 100)
        self.assertIsNotNone(ui.find(".//widget[@name='warningTextEdit']"))

    def test_graphic_eq_clamps_persisted_and_dragged_table_width(self) -> None:
        header = (GUIS / "GraphicEQFilterGUI.h").read_text(encoding="utf-8")
        source = (GUIS / "GraphicEQFilterGUI.cpp").read_text(encoding="utf-8")
        ui = ET.parse(GUIS / "GraphicEQFilterGUI.ui").getroot()

        self.assertIn("int minimumTableWidth() const;", header)
        self.assertIn("int maximumTableWidth() const;", header)
        self.assertIn("void setTableWidth(int width);", header)
        self.assertIn("int maximumViewHeight() const;", header)
        self.assertIn("void setViewHeight(int height);", header)
        self.assertIn("filterTable->installEventFilter(this)", source)
        self.assertIn("QEvent::Resize", source)
        self.assertIn("QEvent::LayoutRequest", source)
        self.assertIn("currentScreen->availableGeometry().width()", source)
        self.assertIn("currentScreen->availableGeometry().height()", source)
        self.assertIn("qBound(minimumTableWidth(), width, maximumTableWidth())", source)
        self.assertIn("qBound(minimumViewHeight(), height, maximumViewHeight())", source)
        self.assertIn("toDouble(&validTableWidth)", source)
        self.assertIn("std::isfinite(storedTableWidth)", source)
        self.assertIn("toDouble(&validViewHeight)", source)
        self.assertIn("std::isfinite(storedViewHeight)", source)
        self.assertIn(
            'prefs.insert("viewHeight", GUIHelper::invScale(', source
        )
        self.assertIn("setTableWidth(TABLE_RESIZE_ORIGIN - size.width())", source)
        self.assertIn("setViewHeight(size.height())", source)

        table = ui.find(".//widget[@name='tableWidget']")
        self.assertIsNotNone(table)
        self.assertEqual(
            property_text(table, "sizeAdjustPolicy"),
            "QAbstractScrollArea::SizeAdjustPolicy::AdjustIgnored",
        )

    def test_vst_editor_is_screen_bounded_and_scroll_reachable(self) -> None:
        header = (GUIS / "VSTPluginFilterGUIDialog.h").read_text(
            encoding="utf-8"
        )
        source = (GUIS / "VSTPluginFilterGUIDialog.cpp").read_text(
            encoding="utf-8"
        )
        ui = ET.parse(GUIS / "VSTPluginFilterGUIDialog.ui").getroot()

        scroll_area = ui.find(".//widget[@name='editorScrollArea']")
        self.assertIsNotNone(scroll_area)
        self.assertEqual(scroll_area.attrib.get("class"), "QScrollArea")
        self.assertEqual(property_text(scroll_area, "widgetResizable"), "false")
        self.assertEqual(
            property_text(scroll_area, "horizontalScrollBarPolicy"),
            "Qt::ScrollBarAsNeeded",
        )
        self.assertEqual(
            property_text(scroll_area, "verticalScrollBarPolicy"),
            "Qt::ScrollBarAsNeeded",
        )
        frame = scroll_area.find(".//widget[@name='frame']")
        self.assertIsNotNone(frame)

        self.assertIn("void setEditorSize(int width, int height);", header)
        self.assertIn("void constrainToAvailableScreen();", header)
        self.assertIn("MAX_EDITOR_DIMENSION", source)
        self.assertIn("targetScreen->availableGeometry()", source)
        self.assertIn("setMaximumSize(maximumDialogSize)", source)
        self.assertIn("QWindow::screenChanged", source)
        self.assertIn("qBound(1, width, MAX_EDITOR_DIMENSION)", source)
        self.assertEqual(source.count("ui->frame->setFixedSize("), 2)
        self.assertLess(
            source.index("ui->setupUi(this)"),
            source.index("ui->frame->winId()"),
        )
        self.assertIn("reparented after startEditing()", source)
        self.assertIn("setEditorSize(w, h)", source)
        self.assertIn("setEditorSize(width, height)", source)

    def test_copy_assignments_reflow_without_stale_rows_or_horizontal_scroll(self) -> None:
        source = (GUIS / "CopyFilterGUIForm.cpp").read_text(encoding="utf-8")
        header = (GUIS / "CopyFilterGUIForm.h").read_text(encoding="utf-8")
        form_ui = ET.parse(GUIS / "CopyFilterGUI.ui").getroot()
        row_ui = ET.parse(GUIS / "CopyFilterGUIRow.ui").getroot()

        assignment_scroll = form_ui.find(".//widget[@name='scrollArea']")
        self.assertIsNotNone(assignment_scroll)
        self.assertEqual(
            property_text(assignment_scroll, "horizontalScrollBarPolicy"),
            "Qt::ScrollBarAlwaysOff",
        )
        self.assertEqual(property_text(assignment_scroll, "widgetResizable"), "true")
        graph = form_ui.find(".//widget[@name='graphicsView']")
        self.assertIsNotNone(graph)
        self.assertEqual(
            property_text(graph, "sizeAdjustPolicy"),
            "QAbstractScrollArea::AdjustIgnored",
        )

        self.assertIn("QList<FormRow> formRows;", header)
        for token in (
            "setMinimumWidth(0)",
            "QSizePolicy::Ignored",
            "formRows.clear()",
            "gridLayout->takeAt(0)",
            "widget->hide()",
            "widget->deleteLater()",
            "addSummandButton->setAutoRaise(true)",
            "removeButton->setAutoRaise(true)",
            "setAccessibleName",
        ):
            with self.subTest(copy_contract=token):
                self.assertIn(token, source)

        mode_combo = row_ui.find(".//widget[@name='modeComboBox']")
        channel_combo = row_ui.find(".//widget[@name='channelComboBox']")
        self.assertIsNotNone(mode_combo)
        self.assertIsNotNone(channel_combo)
        self.assertEqual(
            property_text(mode_combo, "sizeAdjustPolicy"),
            "QComboBox::AdjustToMinimumContentsLengthWithIcon",
        )
        self.assertEqual(
            property_text(channel_combo, "sizeAdjustPolicy"),
            "QComboBox::AdjustToMinimumContentsLengthWithIcon",
        )

    def test_copy_panel_height_preferences_and_dragging_are_bounded(self) -> None:
        source = (GUIS / "CopyFilterGUI.cpp").read_text(encoding="utf-8")

        for token in (
            "constexpr double DEFAULT_HEIGHT = 88.0",
            "constexpr double MINIMUM_HEIGHT = 85.0",
            "constexpr double MAXIMUM_HEIGHT = 360.0",
            "double boundedLogicalHeight(double height)",
            "std::isfinite(height)",
            "toDouble(&validHeight)",
            "!std::isfinite(storedHeight)",
            "QSize(0, GUIHelper::scale(MAXIMUM_HEIGHT))",
            "boundedScaledHeight(size.height())",
            "boundedLogicalHeight(\n\t\tGUIHelper::invScale(ui->scrollArea->height()))",
        ):
            with self.subTest(height_contract=token):
                self.assertIn(token, source)

        self.assertIn(
            "cornerWidget, 1, 1, Qt::AlignRight | Qt::AlignBottom", source
        )
        self.assertNotIn("ui->scrollArea->setCornerWidget(cornerWidget)", source)


if __name__ == "__main__":
    unittest.main()
