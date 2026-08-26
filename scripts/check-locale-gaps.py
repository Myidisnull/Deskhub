import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
strings_h = (root / "core/include/deskhub/ui/Strings.h").read_text(encoding="utf-8")
all_en = set(re.findall(r'LStr\s+\w+\{"([^"]+)"\}', strings_h))

catalog = (root / "core/src/ui/LocaleCatalog.cpp").read_text(encoding="utf-8")
langs = re.findall(r"static const CatalogEntry (k_\w+)\[\]", catalog)
for lang in langs:
    block = catalog.split(f"static const CatalogEntry {lang}[]")[1].split("};")[0]
    keys = set(re.findall(r'\{"([^"]+)"', block))
    missing = sorted(all_en - keys)
    print(f"\n{lang}: {len(keys)} entries, missing {len(missing)}")
    for s in missing:
        print(f"  {s!r}")
