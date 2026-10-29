#!/usr/bin/env python3
"""Sanity checks for a Qt .ts translation file (run by ctest as 'translations').

  * every message is translated (no 'unfinished', no empty translation)
  * %1..%9 / %n placeholders match between source and translation
  * plural messages carry the number of forms the language needs
  * no leftover obsolete entries

Usage: check_translations.py translations/platter_ru.ts
"""
import re
import sys
import xml.etree.ElementTree as ET

FORMS = {"ru": 3, "ru_RU": 3}

def placeholders(s):
    return sorted(re.findall(r"%(?:n|[1-9])", s))

def main(path):
    root = ET.parse(path).getroot()
    lang = root.get("language", "")
    need = FORMS.get(lang)
    problems = []
    total = 0
    for ctx in root.findall("context"):
        cname = ctx.findtext("name")
        for m in ctx.findall("message"):
            total += 1
            src = m.findtext("source") or ""
            t = m.find("translation")
            tag = f"[{cname}] {src[:60]!r}"
            if t is None or t.get("type") in ("unfinished", "obsolete", "vanished"):
                problems.append(f"{tag}: untranslated ({None if t is None else t.get('type')})")
                continue
            if m.get("numerus") == "yes":
                forms = [(f.text or "") for f in t.findall("numerusform")]
                if need and len(forms) != need:
                    problems.append(f"{tag}: {len(forms)} plural forms, need {need}")
                texts = forms
            else:
                texts = [t.text or ""]
            if not any(x.strip() for x in texts):
                problems.append(f"{tag}: empty translation")
                continue
            for x in texts:
                if m.get("numerus") != "yes" and placeholders(src) != placeholders(x):
                    problems.append(f"{tag}: placeholders {placeholders(src)} vs {placeholders(x)}")
                    break
                if m.get("numerus") == "yes" and any(p for p in placeholders(x) if p not in placeholders(src)):
                    problems.append(f"{tag}: placeholder not in source: {placeholders(x)}")
                    break
    for p in problems:
        print(p)
    print(f"{total} messages, {len(problems)} problem(s)")
    return 1 if problems else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
