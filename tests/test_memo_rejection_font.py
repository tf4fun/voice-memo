"""Check generated cmap data for every new rejection-dialog string at both sizes."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def coverage(size):
    text = (ROOT / f"assets/fonts/memo_font_{size}.c").read_text()
    arrays = {
        name: [int(n, 0) for n in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]
        for name, body in re.findall(
            r"static const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};", text, re.S
        )
    }
    result = set()
    for start, length, table, kind in re.findall(
        r"\.range_start = (\d+), \.range_length = (\d+).*?"
        r"\.unicode_list = (\w+).*?\.type = (\w+)", text, re.S
    ):
        start, length = int(start), int(length)
        if kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
            result.update(range(start, start + length))
        elif kind == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
            result.update(start + offset for offset in arrays[table])
        else:
            raise AssertionError(f"Unsupported generated cmap {kind}")
    return result


class RejectionFontTest(unittest.TestCase):
    def test_new_ui_strings(self):
        source = (ROOT / "main/memo_app.c").read_text()
        dialog = source.split("static void poll_rejected_choice(void)", 1)[1].split(
            "static void enqueue_input", 1)[0]
        strings = [json.loads('"' + s + '"') for s in re.findall(r'"((?:[^"\\]|\\.)*)"', dialog)]
        strings += ["待重试", "上传失败"]
        for size in (16, 20):
            present = coverage(size)
            for text in strings:
                for char in text:
                    if ord(char) >= 32:
                        self.assertIn(ord(char), present, f"{size}px missing U+{ord(char):04X}")

    def test_known_missing_negative(self):
        for size in (16, 20):
            self.assertNotIn(0x9F98, coverage(size))


if __name__ == "__main__":
    unittest.main()
