---
name: juce-utf8-strings
description: >-
  Force UTF-8 decoding for every narrow string passed to JUCE UI/APIs in
  aufx-test / Quadraverse native code. Use whenever adding or editing
  juce::String literals, button/menu/dialog text, status labels, FileChooser
  titles, AlertWindow messages, or converting const char* into juce::String.
  Prevents mojibake from ellipsis, em dashes, arrows, infinity, and other
  non-ASCII UTF-8.
---

# JUCE UTF-8 string literals (aufx-test / Quadraverse)

## Rule

**Never** pass a UTF-8 narrow C string through `juce::String(const char*)` (or APIs that do that implicitly). That constructor only accepts ASCII; multi-byte UTF-8 becomes mojibake.

Always wrap with the project helper:

```cpp
#include "Utf8.h"

label.setText (utf8 ("Load…"), juce::dontSendNotification);
button.setButtonText (utf8 ("Import SSX…"));
menu.addItem (id, utf8 ("Hardware Audio Setup…"));
juce::AlertWindow w (utf8 ("Comparison Report"), utf8 ("…"), …);
status.setText (utf8 ("Imported SSX → ") + file.getFileName(), …);
names.add (utf8 (cStringFromTable));  // when seeding juce::String from const char*
```

Helper lives at [`native/plugin_host_app/Source/Utf8.h`](native/plugin_host_app/Source/Utf8.h):

```cpp
inline juce::String utf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}
```

## When it applies

Use `utf8(...)` for **every** narrow string that becomes a `juce::String` for UI or user-visible text, including:

- In-class / member initializers: `juce::TextButton loadButton { utf8 ("Load…") };`
- `setText` / `setButtonText` / `setTooltip` / `addItem` / `AlertWindow` / `FileChooser` / `PopupMenu`
- Concatenation left-hand UTF-8 pieces: `utf8 ("Settings → ") + path`
- `const char*` tables (e.g. `QvParameters.h` names) when assigned into `juce::String` / `StringArray`

ASCII-only literals still go through `utf8(...)` in UI code for consistency with AUFX Explorer.

## Exceptions

- Comments (not passed to JUCE)
- Non-JUCE APIs (`std::string`, `std::cerr`, file formats, JSON keys that stay `std::string`)
- Strings already typed as `juce::String` (no re-wrap needed)

## Checklist before finishing a native UI change

1. `#include "Utf8.h"` in every `.cpp`/`.h` that builds UI strings
2. Grep new/edited code for `…`, `—`, `–`, `→`, `←`, `∞`, `↔`, smart quotes
3. Grep for `juce::String ("` / `setText ("` / `setButtonText ("` / `AlertWindow ("` and wrap
4. Prefer readable UTF-8 in the literal (`"Load…"`) over `\x` escapes unless matching existing Explorer style for infinity (`utf8InfinityDb()`)
