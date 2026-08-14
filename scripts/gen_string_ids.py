#!/usr/bin/env python3
import re
from pathlib import Path

src = Path(__file__).resolve().parents[1] / "core/include/deskhub/ui/Strings.h"
text = src.read_text(encoding="utf-8")
consts = re.findall(
    r'inline constexpr const char\* (k\w+)\s*=\s*"((?:\\.|[^"\\])*)"', text
)
aliases = re.findall(
    r"inline constexpr const char\* (k\w+)\s*=\s*(k\w+);", text
)
print(f"consts={len(consts)} aliases={len(aliases)}")
for name, value in consts:
    print(f"{name}\t{value}")
for a, b in aliases:
    print(f"{a}\t={b}")
