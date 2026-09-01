#!/usr/bin/env python3
"""Contract checks for Potion's Bionic Reading font family."""
import argparse
import hashlib
import shutil
import subprocess
import tempfile
from pathlib import Path
from fontTools.ttLib import TTFont
import generate_fonts

def feature(font, tag):
    for record in font["GSUB"].table.FeatureList.FeatureRecord:
        if record.FeatureTag == tag:
            return list(record.Feature.LookupListIndex)
    raise AssertionError("missing " + tag)

def win_name(font, name_id):
    for record in font["name"].names:
        if record.nameID == name_id and record.platformID == 3 and record.langID == 0x409:
            return record.toUnicode()
    raise AssertionError("missing name %d" % name_id)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--font-dir", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as raw:
        second = Path(raw)
        for style, (filename, _) in generate_fonts.FACES.items():
            source = args.source_dir / filename
            expected = args.font_dir / ("PotionFastSans-%s.otf" % style.replace(" ", ""))
            rebuilt = second / expected.name
            original_font = TTFont(source)
            original_ccmp = feature(original_font, "ccmp")
            original_calt = feature(original_font, "calt")
            generate_fonts.build(source, rebuilt, style)
            assert digest(expected) == digest(rebuilt), "non-deterministic output: " + style
            font = TTFont(expected)
            assert feature(font, "ccmp") == original_ccmp + [i for i in original_calt if i not in original_ccmp]
            assert feature(font, "ccmp")[:len(original_ccmp)] == original_ccmp
            assert win_name(font, 1) == generate_fonts.FAMILY
            assert win_name(font, 2) == style
            assert win_name(font, 6).startswith("PotionFastSans-")
            assert win_name(font, 13).startswith("This Font Software is licensed under the SIL Open Font License")
            assert win_name(font, 14) == "https://openfontlicense.org"
    hb = shutil.which("hb-shape")
    if hb:
        regular = args.font_dir / "PotionFastSans-Regular.otf"
        plain = subprocess.check_output([hb, str(regular), "Reading faster improves attention.", "--features=-ccmp,-calt"], text=True)
        shaped = subprocess.check_output([hb, str(regular), "Reading faster improves attention.", "--features=ccmp,-calt"], text=True)
        assert plain != shaped, "ccmp does not activate Fast-Font substitutions"
        for sample in ("café résumé naïve façade", "cafe\u0301 re\u0301sume\u0301", "شبکه فارسی", "中文 日本語"):
            subprocess.check_output([hb, str(regular), sample, "--features=ccmp,-calt"], text=True)
    print("four-face metadata, deterministic output, preserved ccmp prefix, and host shaping: PASS")

if __name__ == "__main__":
    main()
