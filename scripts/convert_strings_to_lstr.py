#!/usr/bin/env python3
from pathlib import Path
import re

p = Path(__file__).resolve().parents[1] / "core/include/deskhub/ui/Strings.h"
text = p.read_text(encoding="utf-8")
if "Locale.h" not in text:
    text = text.replace('#pragma once\n', '#pragma once\n#include "deskhub/ui/Locale.h"\n', 1)

keep = {"kAppVersion", "kProjectUrl"}


def repl_const(m: re.Match[str]) -> str:
    name = m.group(1)
    lit = m.group(2)
    if name in keep:
        return m.group(0)
    return f"inline constexpr LStr {name}{{{lit}}};"


text = re.sub(
    r'inline constexpr const char\* (k\w+)\s*=\s*("(?:\\.|[^"\\])*");',
    repl_const,
    text,
)
text = re.sub(
    r"inline constexpr const char\* (k\w+)\s*=\s*(k\w+);",
    r"inline constexpr LStr \1 = \2;",
    text,
)

needle = "inline std::string TrimAscii"
extra = (
    'inline constexpr LStr kLanguageLabel{"Language"};\n'
    'inline constexpr LStr kLanguageSystem{"System default"};\n'
    'inline constexpr LStr kSettingsSectionLanguage{"Language"};\n'
    'inline constexpr LStr kLanguageRestartHint{'
    '"The new language applies after you restart the app."};\n\n'
)
if "kLanguageLabel" not in text:
    text = text.replace(needle, extra + needle)

p.write_text(text, encoding="utf-8")
print("LStr", text.count("inline constexpr LStr"))
print("const char* k", len(re.findall(r"inline constexpr const char\* k", text)))
