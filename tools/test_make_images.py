"""Tests for the built-in visualizer icons (tools/make_images.py): python3 -m unittest discover -s tools -p 'test_*.py'

src/icons.c should hold the Chart's masks (assets/icon_masks.py) as A8 images, with their revs: run make_images.py
again after copying a newer icon_masks.py from metarmap2.
"""

import hashlib
import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import make_images  # noqa: E402

ICONS_C = (make_images.ROOT / "src" / "icons.c").read_text()


def c_bytes(name):
    """The bytes of `static const uint8_t <name>_data[]` in src/icons.c."""
    found = re.search(rf"static const uint8_t {name}_data\[\] = {{(.*?)}};", ICONS_C, re.S)
    return bytes(int(v, 16) for v in re.findall(r"0x([0-9a-f]{2})", found.group(1)))


class BuiltInIcons(unittest.TestCase):
    def test_the_masks_are_the_charts(self):
        masks, revs = make_images.chart_masks()  # checks each mask's size and rev
        for icon in make_images.COMPILED:
            with self.subTest(icon=icon):
                self.assertEqual(c_bytes(f"icon_vis_{icon}"), masks[icon])
                self.assertEqual(hashlib.sha1(masks[icon]).hexdigest()[:8], revs[icon])
                self.assertIn(f'{{"{icon}", "{revs[icon]}", &icon_vis_{icon}}},', ICONS_C)

    def test_a8_twenty_a_side(self):
        for icon in make_images.COMPILED:
            block = re.search(rf"const lv_image_dsc_t icon_vis_{icon} = {{(.*?)\n}};", ICONS_C, re.S).group(1)
            self.assertIn(".cf = LV_COLOR_FORMAT_A8", block)
            self.assertIn(".stride = 20", block)

    def test_hourglass_comes_from_the_chart(self):
        # FAA Delays' icon isn't built in: the board test of fetching one needs a visualizer this firmware lacks
        self.assertNotIn("hourglass", make_images.COMPILED)
        self.assertNotIn("icon_vis_hourglass", ICONS_C)


if __name__ == "__main__":
    unittest.main()
