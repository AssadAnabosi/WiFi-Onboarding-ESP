import gzip
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import build_assets  # noqa: E402


class BuildAssetsTest(unittest.TestCase):
    def build_into(self, out_dir):
        build_assets.build(out_dir=Path(out_dir))
        return {name: (Path(out_dir) / name).read_bytes() for name in ("WebAssets.h", "Version.h")}

    def test_output_is_identical_on_two_runs(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            self.assertEqual(self.build_into(first), self.build_into(second))

    def test_gzip_has_no_timestamp_or_name(self):
        data = b"hello"
        compressed = build_assets.zopfli.gzip.compress(data)
        self.assertEqual(compressed[4:8], b"\x00\x00\x00\x00")  # MTIME
        self.assertEqual(compressed[3] & 0x08, 0)  # FNAME flag
        self.assertEqual(gzip.decompress(compressed), data)

    def test_urls(self):
        self.assertEqual(build_assets.url_for("index.html"), "/")
        self.assertEqual(build_assets.url_for("settings.html"), "/settings")
        self.assertEqual(build_assets.url_for("css/index.css"), "/css/index.css")
        self.assertEqual(build_assets.url_for("favicon.ico"), "/favicon.ico")

    def test_version_is_validated(self):
        with tempfile.TemporaryDirectory() as tmp:
            version_file = Path(tmp) / "VERSION"
            version_file.write_text("1.2.3\n", encoding="utf-8")
            self.assertEqual(build_assets.read_version(version_file), ("1.2.3", (1, 2, 3)))
            for bad in ("1.2", "v1.2.3", "1.2.3-beta"):
                version_file.write_text(bad, encoding="utf-8")
                with self.assertRaises(ValueError):
                    build_assets.read_version(version_file)

    def test_version_is_injected_into_pages(self):
        with tempfile.TemporaryDirectory() as tmp:
            web = Path(tmp) / "web"
            web.mkdir()
            (web / "index.html").write_text("<footer>v{{VERSION}}</footer>", encoding="utf-8")
            assets = build_assets.collect_assets(web, "9.8.7")
            html = gzip.decompress(assets[0][2]).decode("utf-8")
            self.assertIn("v9.8.7", html)
            self.assertNotIn("{{VERSION}}", html)


if __name__ == "__main__":
    unittest.main()
