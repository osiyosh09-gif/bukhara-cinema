"""Fails when the committed i18n/*.ts files hold other messages than lupdate finds.

    python tools/check_ts_sync.py <committed i18n dir> <i18n dir after lupdate>

The .ts files are committed; normally `cmake --build build --target
update_translations` refreshes them. This check (Linux CI) makes sure no
interface text was added to the code without its catalogue entry, and that
no stale entry stays behind. Line numbers are not compared.
"""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def messages(ts):
    out = set()
    for ctx in ET.parse(ts).getroot().iter("context"):
        name = ctx.findtext("name")
        for msg in ctx.iter("message"):
            tr = msg.find("translation")
            if tr is not None and tr.get("type") in ("obsolete", "vanished"):
                continue
            out.add((name, msg.findtext("source") or ""))
    return out


def main():
    committed, fresh = Path(sys.argv[1]), Path(sys.argv[2])
    bad = 0
    for ts in sorted(committed.glob("*.ts")):
        a, b = messages(ts), messages(fresh / ts.name)
        for ctx, src in sorted(b - a):
            print(f"{ts.name}: missing in the committed file: [{ctx}] {src!r}")
            bad += 1
        for ctx, src in sorted(a - b):
            print(f"{ts.name}: not in the code any more: [{ctx}] {src!r}")
            bad += 1
        print(f"{ts.name}: {len(a)} committed, {len(b)} found by lupdate")
    if bad:
        print("Run the update_translations target and python3 tools/translations.py, then commit i18n/.")
        return 1
    print("translation files match the code")
    return 0


if __name__ == "__main__":
    sys.exit(main())
