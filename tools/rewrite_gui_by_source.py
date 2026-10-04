# -*- coding: utf-8 -*-
"""Regenerate en/zh gui_by_source.ini with full translations."""
from __future__ import unicode_literals

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from extract_locale_strings import collect_tr_strings, decode_c, EN_MAP as EN_BASE, ZH_MAP as ZH_BASE
from gui_by_source_translations import EN_EXTRA, ZH_EXTRA

SKIP_SOURCES = frozenset([
    "\u2014",  # em dash placeholder row
])


def skip_source(src):
    if src in SKIP_SOURCES:
        return True
    return False


def encode_pipe_field(text):
    """Store newlines as \\n so the legacy line-based loader can round-trip."""
    return (text.replace("\\", "\\\\")
                .replace("\r\n", "\\n")
                .replace("\n", "\\n")
                .replace("\r", "\\n")
                .replace("|", "\\|"))


def merge_maps(base, extra):
    m = dict(base)
    m.update(extra)
    return m


def translate_string(src, mapping, lang):
    if src in mapping:
        return mapping[src]
    # Latin / brand tokens unchanged
    if src in ("LLM", "Image", "Other", "Speech", "Embedding", "HTTP %1", "HTTP %1: %2"):
        return src
    if lang == "en" and all(ord(c) < 128 for c in src):
        return src
    return None


def write_ini(path, mapping, strings, lang):
    lines = ["# source|translation — legacy tr() map (newlines as \\n)", ""]
    translated = 0
    skipped = 0
    for src in strings:
        if skip_source(src):
            skipped += 1
            continue
        dst = translate_string(src, mapping, lang)
        if dst is None:
            dst = src
        if dst != src:
            translated += 1
        lines.append(encode_pipe_field(src) + "|" + encode_pipe_field(dst))
    d = os.path.dirname(path)
    if not os.path.isdir(d):
        os.makedirs(d)
    with open(path, "wb") as f:
        f.write(("\n".join(lines) + "\n").encode("utf-8"))
    return len(lines) - 2, translated, skipped


def main():
    strings = collect_tr_strings()
    en_map = merge_maps(EN_BASE, EN_EXTRA)
    zh_map = merge_maps(ZH_BASE, ZH_EXTRA)
    out_en = os.path.join(ROOT, "bin", "config", "strings", "en", "gui_by_source.ini")
    out_zh = os.path.join(ROOT, "bin", "config", "strings", "zh", "gui_by_source.ini")
    en_total, en_diff, en_skip = write_ini(out_en, en_map, strings, "en")
    zh_total, zh_diff, zh_skip = write_ini(out_zh, zh_map, strings, "zh")
    missing_en = [s for s in strings if not skip_source(s)
                  and translate_string(s, en_map, "en") is None
                  and not all(ord(c) < 128 for c in s)]
    missing_zh = [s for s in strings if not skip_source(s)
                  and translate_string(s, zh_map, "zh") is None
                  and not all(ord(c) < 128 for c in s)]
    print("tr strings collected:", len(strings))
    print("en ini lines:", en_total, "source!=translation:", en_diff, "skipped:", en_skip)
    print("zh ini lines:", zh_total, "source!=translation:", zh_diff, "skipped:", zh_skip)
    if missing_en:
        print("missing EN:", len(missing_en))
        for s in missing_en[:20]:
            print(" ", repr(s)[:100])
    if missing_zh:
        print("missing ZH:", len(missing_zh))


if __name__ == "__main__":
    main()
