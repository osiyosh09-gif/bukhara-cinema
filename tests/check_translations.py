"""Fails if any interface text is missing a translation.

Checks every i18n/*.ts file: no unfinished/empty translations, and the
%1/%2 placeholders and the '&' accelerators match the source text.
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

errors = []
folder = Path(sys.argv[1] if len(sys.argv) > 1 else "i18n")
files = sorted(folder.glob("*.ts"))
if not files:
    print(f"no .ts files in {folder}")
    sys.exit(1)

for ts in files:
    root = ET.parse(ts).getroot()
    count = 0
    for ctx in root.iter("context"):
        cname = ctx.findtext("name")
        for msg in ctx.iter("message"):
            src = msg.findtext("source") or ""
            tr = msg.find("translation")
            if tr is not None and tr.get("type") in ("obsolete", "vanished"):
                continue
            count += 1
            text = (tr.text or "") if tr is not None else ""
            if tr is None or tr.get("type") == "unfinished" or not text.strip():
                errors.append(f"{ts.name}: [{cname}] untranslated: {src!r}")
                continue
            if sorted(re.findall(r"%\d", src)) != sorted(re.findall(r"%\d", text)):
                errors.append(f"{ts.name}: [{cname}] placeholders differ: {src!r} -> {text!r}")
            if src.count("&") != text.count("&"):
                errors.append(f"{ts.name}: [{cname}] accelerator '&' differs: {src!r} -> {text!r}")
    print(f"{ts.name}: {count} messages")

if errors:
    print("\n".join(errors))
    sys.exit(1)
print("all translations complete")
