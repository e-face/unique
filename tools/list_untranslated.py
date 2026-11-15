# -*- coding: utf-8 -*-
import io
import os
import re
import sys

SRC = os.path.join(os.path.dirname(__file__), "..", "src")
EN = os.path.join(os.path.dirname(__file__), "..", "bin", "config", "strings", "en", "gui_by_source.ini")


def read_text(path):
    with io.open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def extract_tr(text):
    found = []
    i = 0
    while True:
        j = text.find("tr(", i)
        if j < 0:
            break
        # skip if identifier char before tr
        if j > 0 and (text[j - 1].isalnum() or text[j - 1] == "_"):
            i = j + 3
            continue
        k = j + 3
        while k < len(text) and text[k] in " \t\r\n":
            k += 1
        if k >= len(text) or text[k] != '"':
            i = j + 3
            continue
        parts = []
        while k < len(text) and text[k] == '"':
            k += 1
            buf = []
            while k < len(text):
                ch = text[k]
                if ch == "\\":
                    if k + 1 < len(text):
                        nxt = text[k + 1]
                        mapping = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}
                        buf.append(mapping.get(nxt, nxt))
                        k += 2
                        continue
                if ch == '"':
                    k += 1
                    break
                buf.append(ch)
                k += 1
            parts.append("".join(buf))
            while k < len(text) and text[k] in " \t\r\n":
                k += 1
        if parts:
            found.append("".join(parts))
        i = k
    return found


def load_map(path):
    mapping = {}
    if not os.path.isfile(path):
        return mapping
    with io.open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n").rstrip("\r")
            if not line or line.startswith("#") or line.startswith(";"):
                continue
            sep = line.find("|")
            if sep <= 0:
                continue
            source = line[:sep]
            translation = line[sep + 1:]
            source = source.replace(u"\\\\", u"\x01").replace(u"\\n", u"\n").replace(u"\x01", u"\\")
            translation = translation.replace(u"\\\\", u"\x01").replace(u"\\n", u"\n").replace(u"\x01", u"\\")
            mapping[source] = translation
    return mapping


def has_cyr(text):
    for ch in text:
        if u"\u0400" <= ch <= u"\u04ff":
            return True
    return False


def main():
    strings = []
    for name in os.listdir(SRC):
        if not (name.endswith(".cpp") or name.endswith(".h")):
            continue
        raw = read_text(os.path.join(SRC, name))
        if not isinstance(raw, unicode):
            raw = raw.decode("utf-8")
        for item in extract_tr(raw):
            if not isinstance(item, unicode):
                item = item.decode("utf-8")
            strings.append(item)
    unique = []
    seen = set()
    for item in strings:
        if item not in seen:
            seen.add(item)
            unique.append(item)
    mapping = load_map(EN)
    missing = []
    same = []
    for item in unique:
        if item not in mapping:
            missing.append(item)
        elif has_cyr(item) and mapping[item] == item:
            same.append(item)
    out = os.path.join(os.path.dirname(__file__), "_untranslated.txt")
    with io.open(out, "w", encoding="utf-8") as handle:
        handle.write(u"UNIQUE %d\nMISSING %d\nUNTRANSLATED %d\n" % (len(unique), len(missing), len(same)))
        handle.write(u"\n--- MISSING ---\n")
        for item in missing:
            handle.write(item.replace(u"\n", u"\\n") + u"\n")
        handle.write(u"\n--- SAME ---\n")
        for item in same:
            handle.write(item.replace(u"\n", u"\\n") + u"\n")
    sys.stdout.write("unique %d missing %d same %d\n" % (len(unique), len(missing), len(same)))


if __name__ == "__main__":
    main()
