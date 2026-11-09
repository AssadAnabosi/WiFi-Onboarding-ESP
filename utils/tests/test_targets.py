import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import targets  # noqa: E402

PARTITIONS = """# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1F0000,
app1,     app,  ota_1,   0x200000,0x1F0000,
"""


class TargetNameTest(unittest.TestCase):
    def test_good_names(self):
        for name in ("esp32-4mb", "esp32s3-8mb", "esp32-4mb-psram", "esp32c6-16mb-relay-v2"):
            self.assertEqual(targets.validate_name(name), [], name)

    def test_bad_names(self):
        for name in (
            "ESP32-4mb",  # uppercase
            "esp32_4mb",  # underscore
            "esp32-4mb-",  # trailing dash
            "esp32",  # no flash size
            "esp8266-4mb",  # unknown chip
            "esp32h2-4mb",  # no Wi-Fi
            "esp32p4-16mb",  # no Wi-Fi
            "esp32-3mb",  # unknown flash size
        ):
            self.assertNotEqual(targets.validate_name(name), [], name)


class SketchTest(unittest.TestCase):
    def make_sketch(self, tmp, profiles, default="esp32-4mb"):
        sketch = Path(tmp)
        (sketch / "targets").mkdir()
        body = "profiles:\n" + "".join(f"  {p}:\n    fqbn: esp32:esp32:esp32\n" for p in profiles)
        (sketch / "sketch.yaml").write_text(body + f"default_profile: {default}\n", encoding="utf-8")
        (sketch / "partitions.csv").write_text(PARTITIONS, encoding="utf-8")
        (sketch / "targets" / f"{default}.partitions.csv").write_text(PARTITIONS, encoding="utf-8")
        (sketch / "build_opt.h").write_text(targets.build_opt(default), encoding="utf-8")
        for p in profiles:
            (sketch / "targets" / f"{p}.h").write_text("#pragma once\n", encoding="utf-8")
        return sketch

    def test_repository_targets_are_valid(self):
        self.assertEqual(targets.check(), [])

    def test_missing_files_are_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            sketch = self.make_sketch(tmp, ["esp32-4mb", "esp32-4mb-relay"])
            problems = targets.check(sketch)
            self.assertEqual(len(problems), 1)
            self.assertIn("esp32-4mb-relay.partitions.csv", problems[0])

    def test_prepare_other_target(self):
        with tempfile.TemporaryDirectory() as tmp:
            sketch = self.make_sketch(tmp, ["esp32-4mb", "esp32-4mb-relay"])
            other = PARTITIONS.replace("0x1F0000", "0x180000")
            (sketch / "targets" / "esp32-4mb-relay.partitions.csv").write_text(other, encoding="utf-8")
            targets.prepare("esp32-4mb-relay", commit="abc123", sketch_dir=sketch)
            self.assertEqual((sketch / "partitions.csv").read_text(encoding="utf-8"), other)
            opt = (sketch / "build_opt.h").read_text(encoding="utf-8")
            self.assertIn('-DOTA_TARGET_ID=\\"esp32-4mb-relay\\"', opt)
            self.assertIn('-DFIRMWARE_COMMIT=\\"abc123\\"', opt)

    def test_prepare_default_target_restores_its_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            sketch = self.make_sketch(tmp, ["esp32-4mb", "esp32-8mb"])
            other = PARTITIONS.replace("0x1F0000", "0x3F0000")
            (sketch / "targets" / "esp32-8mb.partitions.csv").write_text(other, encoding="utf-8")
            targets.prepare("esp32-8mb", sketch_dir=sketch)
            targets.prepare("esp32-4mb", sketch_dir=sketch)
            self.assertEqual((sketch / "partitions.csv").read_text(encoding="utf-8"), PARTITIONS)
            self.assertEqual(targets.check(sketch), [])

    def test_partitions_copy_must_match_default_target(self):
        with tempfile.TemporaryDirectory() as tmp:
            sketch = self.make_sketch(tmp, ["esp32-4mb"])
            (sketch / "partitions.csv").write_text(PARTITIONS.replace("0x1F0000", "0x180000"), encoding="utf-8")
            problems = targets.check(sketch)
            self.assertEqual(len(problems), 1)
            self.assertIn("partitions.csv does not match", problems[0])

    def test_size_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            sketch = self.make_sketch(tmp, ["esp32-4mb"])
            binary = Path(tmp) / "fw.bin"
            for size, level in ((1000, "ok"), (0x1F0000 * 95 // 100, "warning"), (0x1F0000 + 1, "error")):
                binary.write_bytes(b"\0" * size)
                self.assertEqual(targets.check_size("esp32-4mb", binary, sketch)[0], level)

    def test_partition_sizes(self):
        self.assertEqual(targets.parse_size("0x1F0000"), 0x1F0000)
        self.assertEqual(targets.parse_size("1920K"), 1920 * 1024)
        self.assertEqual(targets.parse_size("2M"), 2 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
