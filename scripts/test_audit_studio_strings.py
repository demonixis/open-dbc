#!/usr/bin/env python3
"""Check what the Studio strings audit reports on small sources."""

import tempfile
import unittest
from pathlib import Path

import audit_studio_strings as Audit


def Run(Source: str, Catalogue: tuple[str, ...] = (), Verbatim: tuple[str, ...] = ()) -> Audit.Findings:
    return Audit.Audit({"panel.cpp": Source}, set(Catalogue), set(Verbatim), set(), {})


class StudioStringsAuditTests(unittest.TestCase):
    def test_label_outside_a_translator_is_reported_with_its_line(self) -> None:
        Result = Run('\nImGui::Button("Save");')
        self.assertEqual(Result.Unwrapped, ["panel.cpp:2: 'Save'"])

    def test_wrapped_labels_ids_and_formats_are_fine(self) -> None:
        Source = """
        ImGui::Button(TrId("Save"));
        ImGui::Button("##hidden");
        ImGui::Text("%d / %d", A, B);
        ImGui::SeparatorText(Flag ? Tr("Save") : Tr("Save"));
        ImGui::BeginCombo("##kind", Value.c_str());
        """
        self.assertEqual(Run(Source, ("Save",)).Total(), 0)

    def test_every_displayed_argument_counts(self) -> None:
        Result = Run('ImGui::SetTooltip("%s", "Close"); ImGui::TextDisabled(Busy ? "Busy" : "Idle");')
        self.assertEqual(len(Result.Unwrapped), 3)

    def test_named_literal_shown_later_is_reported(self) -> None:
        Result = Run('constexpr const char* Popup = "Unsaved changes"; ImGui::BeginPopupModal(Popup, nullptr);')
        self.assertEqual(Result.Unwrapped, ["panel.cpp:1: 'Unsaved changes'"])

    def test_translated_printf_format_is_reported(self) -> None:
        Result = Run('ImGui::Text(Tr("Saved"));', ("Saved",))
        self.assertEqual(len(Result.TranslatedFormats), 1)

    def test_status_messages_count_even_when_short(self) -> None:
        self.assertEqual(Run('Status_ = "Stopped";').Unwrapped, ["panel.cpp:1: 'Stopped'"])

    def test_sentences_are_found_anywhere(self) -> None:
        Source = 'Error = "Cannot read the file"; Name = "main.dba"; Key = "app title"; DBC_LOG_WARN("Cannot log this");'
        self.assertEqual(Run(Source).Sentences, ["panel.cpp:1: 'Cannot read the file'"])

    def test_sentence_in_the_catalogue_reaches_tr_through_a_variable(self) -> None:
        self.assertEqual(Run('Message = "Cannot read the file";', ("Cannot read the file",)).Total(), 0)

    def test_verbatim_list_exempts_a_literal(self) -> None:
        self.assertEqual(Run('ImGui::Button("DBPro");', Verbatim=("DBPro",)).Total(), 0)

    def test_helper_labels_must_be_in_the_catalogue(self) -> None:
        Result = Run('ModeButton("Scene", Active, Enabled, "Scene (Ctrl+2)");', ("Scene",))
        self.assertEqual(Result.Missing, ["panel.cpp:1: 'Scene (Ctrl+2)'"])

    def test_helper_showing_its_label_as_it_is_needs_it_translated(self) -> None:
        self.assertEqual(Run('TextField("Country code", Value);').Unwrapped, ["panel.cpp:1: 'Country code'"])
        self.assertEqual(Run('TextField(TrId("Country code"), Value);', ("Country code",)).Total(), 0)

    def test_missing_and_stale_keys(self) -> None:
        Result = Run('ImGui::Button(TrId("Save##Toolbar"));', ("Open",))
        self.assertEqual(Result.Missing, ["panel.cpp:1: 'Save'"])
        self.assertEqual(Result.Stale, ["'Open'"])

    def test_text_read_at_run_time_must_be_in_the_catalogue(self) -> None:
        Result = Audit.Audit({}, {"Fog"}, set(), set(), {"Fog": "fog.dba", "Lighting": "lighting.dba"})
        self.assertEqual(Result.Missing, ["lighting.dba: 'Lighting'"])
        self.assertEqual(Result.Stale, [])

    def test_snippet_headers_give_their_name_and_description(self) -> None:
        with tempfile.TemporaryDirectory() as Folder:
            (Path(Folder) / "fog.dba").write_text("rem Snippet: Fog\nrem Description: Some fog.\nfog on\n", encoding="utf-8")
            self.assertEqual(sorted(Audit.ReadSnippetTexts(Path(Folder))), ["Fog", "Some fog."])

    def test_adjacent_literals_and_escapes_form_one_key(self) -> None:
        Result = Run('ImGui::SetTooltip("%s", Tr("First line\\n" "second line"));', ("First line\nsecond line",))
        self.assertEqual(Result.Total(), 0)

    def test_comments_and_directives_are_ignored(self) -> None:
        self.assertEqual(Run('// ImGui::Button("Save");\n#include "Some Header.hpp"\n').Total(), 0)


if __name__ == "__main__":
    unittest.main()
