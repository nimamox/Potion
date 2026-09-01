#!/usr/bin/env python3
"""Build Potion's deterministic Fast Sans production font family.

Source files are the four Fast Sans faces from Born2Root/Fast-Font commit
aeae0775d9251365eae3b133cbf26ce0366f6108.
"""
import argparse
import hashlib
from pathlib import Path
from fontTools.ttLib import TTFont

COMMIT = "aeae0775d9251365eae3b133cbf26ce0366f6108"
FAMILY = "Potion Fast Sans"
FACES = {
    "Regular": ("Fast_Sans_Regular.otf", "be3f8ab046c2ddb11ab08e087ac8300969bc19367a4a1ebd93853606346c27e1"),
    "Italic": ("Fast_Sans_Italic.otf", "80446f7451ec6e730f1968d6abd6a9811897f303effa770c97eb2cb0bc8f4680"),
    "Bold": ("Fast_Sans_Bold.otf", "b626ddaf5808212290da4996cec6ca6a6b0ec58d37a926e2f05a0ec827fccfa3"),
    "Bold Italic": ("Fast_Sans_BoldItalic.otf", "51705f3bef96193ad414edb8b0537cc7c5aa4c0de30cf4ec48537a624b358d69"),
}

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def set_name(font, name_id, value):
    table = font["name"]
    for record in list(table.names):
        if record.nameID == name_id:
            table.setName(value, name_id, record.platformID, record.platEncID, record.langID)
    table.setName(value, name_id, 3, 1, 0x409)
    table.setName(value, name_id, 1, 0, 0)

def feature(font, tag):
    for record in font["GSUB"].table.FeatureList.FeatureRecord:
        if record.FeatureTag == tag:
            return record.Feature
    raise RuntimeError("missing GSUB feature: " + tag)

def build(source, output, style):
    font = TTFont(source, recalcTimestamp=False)
    calt = list(feature(font, "calt").LookupListIndex)
    ccmp_feature = feature(font, "ccmp")
    original_ccmp = list(ccmp_feature.LookupListIndex)
    ccmp_feature.LookupListIndex = original_ccmp + [i for i in calt if i not in original_ccmp]
    ccmp_feature.LookupCount = len(ccmp_feature.LookupListIndex)

    compact = style.replace(" ", "")
    ps_family = "PotionFastSans"
    set_name(font, 1, FAMILY)
    set_name(font, 2, style)
    set_name(font, 4, FAMILY + " " + style)
    set_name(font, 6, ps_family + "-" + compact)
    set_name(font, 16, FAMILY)
    set_name(font, 17, style)
    set_name(font, 3, "Potion;Fast-Font %s;%s" % (COMMIT[:12], style))
    set_name(font, 13, "This Font Software is licensed under the SIL Open Font License, Version 1.1. This license is available at: https://openfontlicense.org")
    set_name(font, 14, "https://openfontlicense.org")
    font["head"].modified = 0
    output.parent.mkdir(parents=True, exist_ok=True)
    font.save(output, reorderTables=False)
    return original_ccmp, calt

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    for style, (filename, expected) in FACES.items():
        source = args.source_dir / filename
        actual = sha256(source)
        if actual != expected:
            raise SystemExit("unexpected source hash for %s: %s" % (filename, actual))
        output = args.output_dir / ("PotionFastSans-%s.otf" % style.replace(" ", ""))
        original, added = build(source, output, style)
        print("%s %s ccmp=%s + calt=%s sha256=%s" %
              (style, output, original, added, sha256(output)))

if __name__ == "__main__":
    main()
