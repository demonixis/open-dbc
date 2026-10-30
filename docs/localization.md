# Localization

The Studio's interface exists in English and French. English is the source
language: its text is the key, so a missing translation shows the English.
**View > Language** picks a language or follows the system. Only the Studio
activates a language: the command line, `opendbc-runner` and the tests stay in
English.

## What is translated

Everything the Studio itself says: menus, labels, tooltips, dialogs, the status
bar, its messages in the Output panel, export checks, errors and notes, and the
names and descriptions of the built-in snippets.

These stay in English:

- `DBC_LOG_*` logs, the command line and program output;
- what the language, interpreter and engine layers report, such as compiler and
  project diagnostics, since they know nothing of the interface. One exception: a
  dialect left undecided, which the resolver flags and the Studio words itself;
- the command reference, type names in signatures, and BASIC commands inside a
  translation;
- generated BASIC, `REM @` lines, snippet bodies and template comments;
- JSON and `.dbpro` keys, Android manifest values, log levels, and product and
  platform names.

Changing the language never alters a project: what `Tr` returns must never reach
a project file, generated code or a setting.

## In code

`tools/studio/src/core/i18n.hpp`:

| Call | For |
| --- | --- |
| `Tr("Save")` | text; safe on any thread |
| `TrC("transform part\|scale")` | a homonym, with its own catalogue entry |
| `TrId("Output")` | anything with an ImGui ID, which then ignores the language; UI thread |
| `TrFmt("Moved {0} to {1}", {A, B})` | text with values, which a translation may reorder |

- Pass translated text as an argument, never as a format:
  `ImGui::TextDisabled("%s", Tr("..."))`.
- Write whole sentences with placeholders rather than joining fragments.
- `Tr` returns its argument when there is no translation, so never pass a
  temporary's `c_str()` beyond the full expression.
- Use one `TrId` value to open and to begin a popup.
- Measure text with `CalcTextSize` and wrap sentences with `PushTextWrapPos`:
  a width that fits English will clip another language.

## Catalogues

`tools/studio/locales/<code>.json` is a flat object embedded at build time: its
language's own `@name`, then source text to translation, sorted by code point.

French uses `’` and `…`, and ` : ` with a regular space. "snippet" stays, a stub is
a "bouchon de compatibilité" and Build is "Génération".

To add a language, copy `fr.json` to `<code>.json` and translate it. `fr.json`
defines the audited keys and is the catalogue the placeholder test reads, so extend
both to the new file. The default font and its Noto Sans fallback cover Latin
scripts; check `Theme::AddFonts` for any other.

## Audit

`scripts/audit_studio_strings.py --check` runs in CTest as `studio_strings_audit`.
It reports interface literals outside a `Tr*` call, translated text used as a
format, keys and built-in snippet texts absent from `fr.json`, and catalogue keys
nothing mentions any more.

A literal that must stay as it is goes in `scripts/data/studio_verbatim_strings.txt`;
a source without interface text goes in `NO_INTERFACE_TEXT` in the script. The audit
reads literals, not meaning: a fragment that does not read as a sentence, such as
one starting in lowercase, escapes it unless it reaches an ImGui label.

## Pseudo-locale

Debug builds also list **Pseudo** under **View > Language**. Every catalogue key
then shows with accents, brackets and about 40% more length: plain English is text
outside the catalogue, and a missing `]` is a layout too tight.
