import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import ui_preview  # noqa: E402

PAGES = ["index", "home", "scan", "settings"]


class UiPreviewTest(unittest.TestCase):
    def page(self, name):
        return (ui_preview.WEB_DIR / f"{name}.html").read_text(encoding="utf-8")

    def test_mock_replaces_every_device_script_once(self):
        for name in PAGES:
            with self.subTest(page=name):
                html = ui_preview.mock_page(self.page(name))
                self.assertEqual(html.count(ui_preview.MOCK_URL), 1)
                self.assertNotRegex(html, r'src="js/(api|home|scan|settings|firmware)\.js"')
                self.assertIn('src="js/app.js"', html)

    def test_mock_defines_what_the_pages_call(self):
        mock = ui_preview.MOCK_JS.read_text(encoding="utf-8")
        defined = set(re.findall(r"^function (\w+)\(", mock, re.M))
        for name in PAGES:
            with self.subTest(page=name):
                called = set(re.findall(r'on(?:click|change|submit)="(\w+)\(', self.page(name)))
                self.assertEqual(called - defined, set())

    def test_page_without_device_scripts_is_refused(self):
        with self.assertRaises(ValueError):
            ui_preview.mock_page("<html></html>")

    def test_version_is_filled_in(self):
        self.assertEqual(ui_preview.fill_version('data-version="{{VERSION}}"', "1.2.3"), 'data-version="1.2.3"')

    def test_urls_map_like_the_device(self):
        web = ui_preview.WEB_DIR
        self.assertEqual(ui_preview.find_file("/", False), (web / "index.html").resolve())
        self.assertEqual(ui_preview.find_file("/settings", False), (web / "settings.html").resolve())
        self.assertEqual(ui_preview.find_file("/settings#firmware", False), (web / "settings.html").resolve())
        self.assertEqual(ui_preview.find_file("/css/style.css", False), (web / "css" / "style.css").resolve())

    def test_mock_script_only_in_mock_mode(self):
        self.assertEqual(ui_preview.find_file(ui_preview.MOCK_URL, True), ui_preview.MOCK_JS)
        self.assertIsNone(ui_preview.find_file(ui_preview.MOCK_URL, False))

    def test_files_outside_the_web_folder_are_not_served(self):
        self.assertIsNone(ui_preview.find_file("/../VERSION", False))
        self.assertIsNone(ui_preview.find_file("/../utils/ui_preview.py", True))


if __name__ == "__main__":
    unittest.main()
