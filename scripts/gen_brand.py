#!/usr/bin/env python3
"""Regenerate Brand.h and sync user-visible product names from brand/Brand.json."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRAND_JSON = ROOT / "brand" / "Brand.json"
BRAND_H = ROOT / "core" / "include" / "deskhub" / "ui" / "Brand.h"


def load_brand() -> dict[str, str]:
    data = json.loads(BRAND_JSON.read_text(encoding="utf-8"))
    required = [
        "product_name",
        "product_description",
        "windows_service_name",
        "code_name",
        "android_log_tag",
        "log_line_tag",
        "data_dir_name",
        "log_file_prefix",
        "project_url",
        "autostart_task_id",
        "autostart_desktop_file",
        "broadcast_extension_name",
    ]
    missing = [k for k in required if k not in data or not str(data[k]).strip()]
    if missing:
        raise SystemExit(f"brand/Brand.json missing keys: {', '.join(missing)}")
    return {k: str(data[k]) for k in required}


def c_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def write_brand_h(b: dict[str, str]) -> None:
    text = f"""#pragma once

namespace deskhub::brand {{

inline constexpr const char* kProductName = "{c_escape(b['product_name'])}";
inline constexpr const char* kProductDescription = "{c_escape(b['product_description'])}";
inline constexpr const char* kWindowsServiceName = "{c_escape(b['windows_service_name'])}";
inline constexpr const char* kCodeName = "{c_escape(b['code_name'])}";
inline constexpr const char* kAndroidLogTag = "{c_escape(b['android_log_tag'])}";
inline constexpr const char* kLogLineTag = "{c_escape(b['log_line_tag'])}";
inline constexpr const char* kDataDirName = "{c_escape(b['data_dir_name'])}";
inline constexpr const char* kLogFilePrefix = "{c_escape(b['log_file_prefix'])}";
inline constexpr const char* kProjectUrl = "{c_escape(b['project_url'])}";
inline constexpr const char* kAutostartTaskId = "{c_escape(b['autostart_task_id'])}";
inline constexpr const char* kAutostartDesktopFile = "{c_escape(b['autostart_desktop_file'])}";
inline constexpr const char* kBroadcastExtensionName = "{c_escape(b['broadcast_extension_name'])}";
inline constexpr const char* kAppToken = "{{app}}";
inline constexpr const char* kServiceToken = "{{service}}";

}}
"""
    BRAND_H.write_text(text, encoding="utf-8", newline="\n")


def pbx_literal(value: str) -> str:
    return f'"{value}"' if " " in value else value


def set_xml_string(path: Path, name: str, value: str) -> bool:
    text = path.read_text(encoding="utf-8")
    pattern = rf'(<string name="{re.escape(name)}">)(.*?)(</string>)'
    updated, n = re.subn(pattern, rf"\g<1>{value}\g<3>", text, count=1, flags=re.S)
    if not n or updated == text:
        return False
    path.write_text(updated, encoding="utf-8", newline="\n")
    return True


def rewrite_pbx_display_names(path: Path, product: str, broadcast: str) -> bool:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    changed = False
    for i, line in enumerate(lines):
        if "INFOPLIST_KEY_CFBundleDisplayName" not in line:
            out.append(line)
            continue
        window = "".join(lines[max(0, i - 30) : min(len(lines), i + 30)])
        use_broadcast = "broadcast" in window.lower()
        value = broadcast if use_broadcast else product
        indent = re.match(r"^(\s*)", line).group(1)
        new_line = f"{indent}INFOPLIST_KEY_CFBundleDisplayName = {pbx_literal(value)};\n"
        if new_line != line:
            changed = True
        out.append(new_line)
    if not changed:
        return False
    path.write_text("".join(out), encoding="utf-8", newline="\n")
    return True


def set_rc_string(path: Path, key: str, value: str) -> bool:
    text = path.read_text(encoding="utf-8")
    updated, n = re.subn(
        rf'(VALUE "{re.escape(key)}",\s*")([^"]*)(")',
        rf"\g<1>{value}\g<3>",
        text,
        count=1,
    )
    if not n or updated == text:
        return False
    path.write_text(updated, encoding="utf-8", newline="\n")
    return True


def sync_windows_rc(path: Path, b: dict[str, str]) -> bool:
    if not path.exists():
        return False
    product = b["product_name"]
    service = b["windows_service_name"]
    changed = False
    for key, value in (
        ("CompanyName", product),
        ("ProductName", product),
        ("FileDescription", service),
        ("InternalName", service),
        ("OriginalFilename", f"{service}.exe"),
    ):
        if set_rc_string(path, key, value):
            changed = True
    return changed


def sync_windows_output_name(path: Path, service: str) -> bool:
    if not path.exists():
        return False
    text = path.read_text(encoding="utf-8")
    updated, n = re.subn(
        r'(set_target_properties\(deskhub_app PROPERTIES OUTPUT_NAME ")([^"]*)("\))',
        rf"\g<1>{service}\g<3>",
        text,
        count=1,
    )
    if not n or updated == text:
        return False
    path.write_text(updated, encoding="utf-8", newline="\n")
    return True


def sync_native(b: dict[str, str]) -> list[str]:
    changed: list[str] = []

    android = ROOT / "client/android/app/src/main/res/values/strings.xml"
    if android.exists() and set_xml_string(android, "app_name", b["product_name"]):
        changed.append(str(android.relative_to(ROOT)))

    for rel in (
        "client/macos/Deskhub.xcodeproj/project.pbxproj",
        "client/ios/Deskhub.xcodeproj/project.pbxproj",
    ):
        path = ROOT / rel
        if path.exists() and rewrite_pbx_display_names(
            path, b["product_name"], b["broadcast_extension_name"]
        ):
            changed.append(rel)

    rc = ROOT / "client/windows/win32/Deskhub.rc"
    if sync_windows_rc(rc, b):
        changed.append(str(rc.relative_to(ROOT)))

    cmake = ROOT / "client/windows/win32/CMakeLists.txt"
    if sync_windows_output_name(cmake, b["windows_service_name"]):
        changed.append(str(cmake.relative_to(ROOT)))

    desktop_script = ROOT / "scripts/stage-linux-pkgroot.sh"
    if desktop_script.exists():
        text = desktop_script.read_text(encoding="utf-8")
        updated, n = re.subn(r"(Name=)([^\n]+)", rf"\g<1>{b['product_name']}", text, count=1)
        if n and updated != text:
            desktop_script.write_text(updated, encoding="utf-8", newline="\n")
            changed.append(str(desktop_script.relative_to(ROOT)))

    notes = ROOT / "client/ios/fastlane/metadata/review_information/notes.txt"
    if notes.exists():
        text = notes.read_text(encoding="utf-8")
        product = b["product_name"]
        # Keep GitHub project URL (code_name) but use product name in prose.
        updated = text
        updated = re.sub(r"\bDeskhub\b(?!/)", product, updated)
        # Restore any accidental product-name injection into the github path.
        updated = updated.replace(
            f"github.com/manhpham90vn/{product}",
            f"github.com/manhpham90vn/{b['code_name']}",
        )
        if updated != text:
            notes.write_text(updated, encoding="utf-8", newline="\n")
            changed.append(str(notes.relative_to(ROOT)))

    return changed


def main() -> int:
    brand = load_brand()
    write_brand_h(brand)
    changed = sync_native(brand)
    print(f"wrote {BRAND_H.relative_to(ROOT)}")
    if changed:
        print("synced:")
        for path in changed:
            print(f"  {path}")
    else:
        print("native resources already matched Brand.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
