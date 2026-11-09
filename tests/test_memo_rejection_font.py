"""Check generated cmap data for every new rejection-dialog string at both sizes."""
import hashlib
import ast
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


def label_width(text):
    """Measure the pinned 16 px font with its actual pair kerning/rounding."""
    source = (ROOT / "assets/fonts/memo_font_16.c").read_text()
    arrays = {
        name: [int(n, 0) for n in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", body)]
        for name, body in re.findall(
            r"static const (?:uint16_t|int8_t) (\w+)\[\]\s*=\s*\{(.*?)\};", source, re.S
        )
    }
    table = source.split("glyph_dsc[] =", 1)[1].split("};", 1)[0]
    advance = [int(n) for n in re.findall(r"\.adv_w = (\d+)", table)]
    glyphs = {}
    for start, length, first, offsets, kind in re.findall(
        r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),"
        r"\s*\.unicode_list = (\w+).*?\.type = (\w+)", source, re.S
    ):
        assert kind in ("LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY", "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY")
        codes = range(int(length)) if kind.endswith("FORMAT0_TINY") else arrays[offsets]
        glyphs.update((int(start) + code, int(first) + index) for index, code in enumerate(codes))
    pair_ids = arrays["kern_pair_glyph_ids"]
    kern = dict(zip(zip(pair_ids[::2], pair_ids[1::2]), arrays["kern_pair_values"]))
    scale = int(re.search(r"\.kern_scale = (\d+)", source).group(1))
    ids = [glyphs[ord(char)] for char in text] + [0]
    return sum((max(0, advance[left] + ((kern.get((left, right), 0) * scale) >> 4)) + 8) >> 4
               for left, right in zip(ids, ids[1:]))


class RejectionFontTest(unittest.TestCase):
    def test_chinese_capture_and_detail_widths(self):
        for status in ("读取中", "读取失败", "录音损坏", "待传", "转写失败", "待转写", "转写中", "正文"):
            for volume in (0, 20, 80, 100):
                text = f"{status}  音量 {volume}%"
                self.assertLessEqual(label_width(text), 192, text)
        for text in ("捕捉灵感", "录音完成", "输入电平"):
            self.assertLessEqual(label_width(text), 152, text)
        for text in ("> 准备", "> 录音", "> 保存"):
            self.assertLessEqual(label_width(text), 130, text)

    def test_terminal_advance_and_host_metrics(self):
        source = (ROOT / "assets/fonts/memo_font_terminal.c").read_text()
        table = source.split("glyph_dsc[] = {", 1)[1].split("};", 1)[0]
        advances = [int(n) for n in re.findall(r"\.adv_w = (\d+)", table)]
        self.assertEqual(advances, [0] + [128] * 96)
        self.assertIn(".line_height = 18,", source)
        self.assertIn(".kern_dsc = NULL", source)
        fixture = (ROOT / "tests/ui_stubs/ui_runtime.c").read_text()
        self.assertIn("memo_font_terminal={.line_height=18}", fixture)
        binding = (ROOT / "main/memo_font.c").read_text().split("memo_font_mono(void)", 1)[1]
        self.assertIn("return &memo_font_terminal;", binding.split("}", 1)[0])
        for text in ("23:59", "--:--", "0:00", "5:00", "99:59"):
            self.assertLessEqual(sum(advances[ord(c)-32+1] for c in text)/16, 40)

    def test_new_ui_strings(self):
        source = (ROOT / "main/memo_app.c").read_text()
        dialog = source.split("static void poll_rejected_choice(void)", 1)[1].split(
            "static void enqueue_input", 1)[0]
        strings = [json.loads('"' + s + '"') for s in re.findall(r'"((?:[^"\\]|\\.)*)"', dialog)]
        strings += ["待重试", "上传失败"]
        settings = (ROOT / "main/memo_ui_settings.c").read_text()
        error = settings.split("void memo_ui_settings_error(void)", 1)[1].split("\n}", 1)[0]
        strings += [json.loads('"' + s + '"') for s in re.findall(r'"((?:[^"\\]|\\.)*)"', error)]
        for size in (16, 20):
            present = coverage(size)
            for text in strings:
                for char in text:
                    if ord(char) >= 32:
                        self.assertIn(ord(char), present, f"{size}px missing U+{ord(char):04X}")

    def test_all_static_ui_literals(self):
        present = {size: coverage(size) for size in (16, 20)}
        paths = [ROOT / "main/memo_app.c", ROOT / "main/memo_font.c"]
        paths += list((ROOT / "main").glob("memo_ui_*.c"))
        for path in paths:
            for token in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', path.read_text(), re.S):
                literal = token.group()
                if not literal.startswith('"'):
                    continue
                text = ast.literal_eval(literal)
                if not any(ord(c) > 127 for c in text):
                    continue
                for size in (16, 20):
                    for char in text:
                        if ord(char) >= 32:
                            self.assertIn(ord(char), present[size], f"{path.name}: {size}px missing U+{ord(char):04X}")

    def test_ui_fixture_metrics_match_generated_fonts(self):
        fixture = (ROOT / "tests/ui_stubs/ui_runtime.c").read_text()
        for size in (16, 20):
            generated = (ROOT / f"assets/fonts/memo_font_{size}.c").read_text()
            height = int(re.search(r"\.line_height = (\d+)", generated).group(1))
            mock_height = int(re.search(rf"memo_font_{size}=\{{\.line_height=(\d+)", fixture).group(1))
            self.assertEqual(height, mock_height)

    def test_font_provenance_manifest(self):
        assets = ROOT / "assets/fonts"
        manifest = json.loads((assets / "manifest.json").read_text())
        inventory = (assets / "charset.txt").read_bytes()
        self.assertEqual(hashlib.sha256(inventory).hexdigest(), manifest["charset_sha256"])
        symbols = set(map(ord, inventory.decode().rstrip("\n")))
        self.assertEqual(len(symbols), manifest["requested_codepoints"])
        level_one = set()
        for lead in range(0xB0, 0xD8):
            for trail in range(0xA1, 0xFF):
                try:
                    level_one.add(ord(bytes([lead, trail]).decode("gb2312")))
                except UnicodeDecodeError:
                    pass
        self.assertEqual(len(level_one), 3755)
        self.assertTrue(level_one.issubset(symbols))
        for size in (16, 20):
            present = coverage(size)
            self.assertEqual(len(present), manifest["emitted_codepoints"])
            self.assertEqual(symbols - present, {0xFF00})
            data = (assets / f"memo_font_{size}.c").read_bytes().split(b"*/", 1)[1]
            self.assertEqual(hashlib.sha256(data).hexdigest(), manifest["outputs"][f"memo_font_{size}.c"]["payload_sha256"])
        self.assertIn("SIL OPEN FONT LICENSE Version 1.1", (assets / "OFL.txt").read_text())
        self.assertIn("Copyright 2014-2021 Adobe", (assets / "NOTICE.txt").read_text())

    def test_known_missing_negative(self):
        for size in (16, 20):
            self.assertNotIn(0x9F98, coverage(size))


if __name__ == "__main__":
    unittest.main()
