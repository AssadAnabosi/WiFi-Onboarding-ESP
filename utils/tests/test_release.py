import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import release  # noqa: E402
import sign  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec  # noqa: E402
from test_signing import PARTITIONS, fake_firmware  # noqa: E402

CHANGELOG = """# Changelog

## [Unreleased]

### Added

- Something not released yet.

## [0.2.0] - 2026-12-01

### Added

- Pull updates.

## [0.1.0] - 2026-11-10

### Added

- First release.

[0.2.0]: https://example.com/compare/v0.1.0...v0.2.0
[0.1.0]: https://example.com/releases/v0.1.0
"""

CONFIG = """#define PROJECT_SLUG "wifi-onboarding"
#define OTA_GITHUB_OWNER "AssadAnabosi"
#define OTA_GITHUB_REPO "WiFi-Onboarding-ESP"
"""


class ReleaseFilesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.changelog = self.dir / "CHANGELOG.md"
        self.changelog.write_text(CHANGELOG, encoding="utf-8")
        self.version = self.dir / "VERSION"
        self.version.write_text("0.2.0\n", encoding="utf-8")
        self.config = self.dir / "ProjectConfig.h"
        self.config.write_text(CONFIG, encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def validate(self, tag, released=()):
        return release.validate(tag, list(released), self.version, self.changelog)

    def test_changelog_section(self):
        self.assertEqual(release.changelog_section("0.2.0", self.changelog), "### Added\n\n- Pull updates.")
        # The last section stops at the link definitions
        self.assertEqual(release.changelog_section("0.1.0", self.changelog), "### Added\n\n- First release.")
        self.assertIsNone(release.changelog_section("0.3.0", self.changelog))
        # "0.1.0" must not match "0.1.01" or similar
        self.assertIsNone(release.changelog_section("0.1", self.changelog))

    def test_valid_release(self):
        self.assertEqual(self.validate("v0.2.0", ["v0.1.0"]), [])
        self.assertEqual(self.validate("v0.2.0"), [])
        # A release already made for this tag (a rerun) does not count as newer
        self.assertEqual(self.validate("v0.2.0", ["v0.1.0", "v0.2.0"]), [])

    def test_tag_must_match_version(self):
        self.assertTrue(any("does not match VERSION" in p for p in self.validate("v0.2.1")))
        self.assertTrue(any("expected vMAJOR" in p for p in self.validate("0.2.0")))
        self.assertTrue(any("expected vMAJOR" in p for p in self.validate("v0.2.0-beta")))

    def test_version_must_be_newer(self):
        problems = self.validate("v0.2.0", ["v0.1.0", "v0.10.0"])
        self.assertTrue(any("not newer than the last release (v0.10.0)" in p for p in problems))
        # Tags that are not versions are ignored
        self.assertEqual(self.validate("v0.2.0", ["nightly", "v0.1.0"]), [])

    def test_changelog_section_required(self):
        self.version.write_text("0.3.0\n", encoding="utf-8")
        self.assertTrue(any("no '## [0.3.0]' section" in p for p in self.validate("v0.3.0")))
        self.changelog.write_text("## [0.3.0] - 2027-01-01\n\n## [0.2.0]\n\n- x\n", encoding="utf-8")
        self.assertTrue(any("is empty" in p for p in self.validate("v0.3.0")))

    def test_check_repo(self):
        self.assertEqual(release.check_repo("AssadAnabosi/WiFi-Onboarding-ESP", self.config), [])
        self.assertEqual(release.check_repo("assadanabosi/wifi-onboarding-esp", self.config), [])
        problems = release.check_repo("someone/fork", self.config)
        self.assertEqual(len(problems), 1)
        self.assertIn("docs/make-it-yours.md", problems[0])


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.key = ec.generate_private_key(ec.SECP256R1())

    def tearDown(self):
        self.tmp.cleanup()

    def add_update(self, target, version=(0, 2, 0), slug="wifi-onboarding"):
        update = sign.sign(fake_firmware(), self.key, version, target, slug, PARTITIONS)
        path = self.dir / f"{slug}-{'.'.join(map(str, version))}-{target}.ota.bin"
        path.write_bytes(update)
        return path

    def test_manifest_and_sums(self):
        a = self.add_update("esp32-4mb")
        b = self.add_update("esp32s3-8mb")
        (self.dir / "wifi-onboarding-0.2.0-esp32-4mb.factory.bin").write_bytes(b"\xff" * 64)
        (self.dir / "THIRD_PARTY_LICENSES.md").write_text("licenses\n", encoding="utf-8")
        release.write_manifest(self.dir, "owner/repo", "v0.2.0", "wifi-onboarding", "- Pull updates.")

        manifest = json.loads((self.dir / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(manifest["project"], "wifi-onboarding")
        self.assertEqual(manifest["version"], "0.2.0")
        self.assertEqual(manifest["notes"], "- Pull updates.")
        self.assertEqual(sorted(manifest["targets"]), ["esp32-4mb", "esp32s3-8mb"])
        entry = manifest["targets"]["esp32-4mb"]
        self.assertEqual(entry["url"], f"https://github.com/owner/repo/releases/download/v0.2.0/{a.name}")
        self.assertEqual(entry["size"], a.stat().st_size)
        self.assertEqual(entry["sha256"], hashlib.sha256(a.read_bytes()).hexdigest())
        self.assertEqual(manifest["targets"]["esp32s3-8mb"]["size"], b.stat().st_size)

        sums = (self.dir / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
        names = [line.split("  ", 1)[1] for line in sums]
        # Every asset, the manifest included, but not the sums file itself
        self.assertEqual(
            names,
            sorted(["THIRD_PARTY_LICENSES.md", "manifest.json", a.name, b.name, "wifi-onboarding-0.2.0-esp32-4mb.factory.bin"]),
        )
        manifest_sum = hashlib.sha256((self.dir / "manifest.json").read_bytes()).hexdigest()
        self.assertIn(f"{manifest_sum}  manifest.json", sums)

    def test_rejects_mismatched_files(self):
        self.add_update("esp32-4mb", version=(0, 1, 9))
        with self.assertRaisesRegex(ValueError, "does not match v0.2.0"):
            release.build_manifest(self.dir, "owner/repo", "v0.2.0", "wifi-onboarding", "")

    def test_rejects_other_project(self):
        self.add_update("esp32-4mb", slug="other-project")
        with self.assertRaisesRegex(ValueError, "project other-project"):
            release.build_manifest(self.dir, "owner/repo", "v0.2.0", "wifi-onboarding", "")

    def test_needs_an_update_file(self):
        with self.assertRaisesRegex(ValueError, "no .ota.bin"):
            release.build_manifest(self.dir, "owner/repo", "v0.2.0", "wifi-onboarding", "")


class InstallIndexTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name) / "firmware"
        self.dir.mkdir()
        # A sketch whose profile order differs from the alphabetical file order
        self.sketch = Path(self.tmp.name) / "esp32"
        (self.sketch / "targets").mkdir(parents=True)
        (self.sketch / "sketch.yaml").write_text(
            "profiles:\n  esp32-8mb:\n    fqbn: x\n  esp32-4mb:\n    fqbn: x\n  esp32-4mb-relay:\n    fqbn: x\n"
            "default_profile: esp32-4mb\n",
            encoding="utf-8",
        )
        for target, title in (("esp32-8mb", "8 MB flash"), ("esp32-4mb", "4 MB flash")):
            (self.sketch / "targets" / f"{target}.h").write_text(
                f'#pragma once\n#define TARGET_TITLE "{title}"\n#define TARGET_DESCRIPTION "{title} boards."\n',
                encoding="utf-8",
            )
        (self.sketch / "targets" / "esp32-4mb-relay.h").write_text("#pragma once\n", encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def add_factory(self, version, target, data=b"\xe9" + b"\xff" * 9000):
        folder = self.dir / version
        folder.mkdir(exist_ok=True)
        (folder / f"wifi-onboarding-{version}-{target}.factory.bin").write_bytes(data)

    def add_notes(self, version, text):
        (self.dir / version / "NOTES.md").write_text(text, encoding="utf-8")

    def build(self):
        return release.build_install_index(self.dir, "wifi-onboarding", self.sketch)

    def test_chip_family(self):
        self.assertEqual(release.chip_family("esp32-4mb"), "ESP32")
        self.assertEqual(release.chip_family("esp32s3-8mb-psram"), "ESP32-S3")
        self.assertEqual(release.chip_family("esp32c61-4mb"), "ESP32-C61")

    def test_trim_image(self):
        self.assertEqual(release.trim_image(b"\xe9\x01" + b"\xff" * 10000), b"\xe9\x01" + b"\xff" * 4094)
        self.assertEqual(len(release.trim_image(b"\x00" * 4096 + b"\xff" * 4096)), 4096)

    def test_index_and_manifests(self):
        self.add_factory("0.10.0", "esp32-4mb")
        self.add_factory("0.10.0", "esp32-8mb")
        self.add_notes("0.10.0", "### Added\n\n- Something.\n")
        self.add_factory("0.9.0", "esp32-4mb")
        (self.dir / "0.10.0" / "wifi-onboarding-0.10.0-esp32-4mb.ota.bin").write_bytes(b"x")  # Ignored
        index = release.write_install_index(self.dir, "wifi-onboarding", self.sketch)

        # Newest first (by version, not by name), notes from NOTES.md, which is then removed
        self.assertEqual([v["version"] for v in index["versions"]], ["0.10.0", "0.9.0"])
        self.assertEqual(index["versions"][0]["notes"], "### Added\n\n- Something.")
        self.assertEqual(index["versions"][1]["notes"], "")
        self.assertFalse((self.dir / "0.10.0" / "NOTES.md").exists())
        self.assertEqual(json.loads((self.dir / "install-index.json").read_text(encoding="utf-8")), index)
        # sketch.yaml order, text from the headers, the default profile recommended
        self.assertEqual(
            index["versions"][0]["targets"],
            [
                {
                    "target": "esp32-8mb",
                    "manifest": "0.10.0/install-esp32-8mb.json",
                    "title": "8 MB flash",
                    "description": "8 MB flash boards.",
                    "recommended": False,
                },
                {
                    "target": "esp32-4mb",
                    "manifest": "0.10.0/install-esp32-4mb.json",
                    "title": "4 MB flash",
                    "description": "4 MB flash boards.",
                    "recommended": True,
                },
            ],
        )
        manifest = json.loads((self.dir / "0.10.0" / "install-esp32-8mb.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["name"], "wifi-onboarding")
        self.assertEqual(manifest["version"], "0.10.0")
        self.assertFalse(manifest["new_install_prompt_erase"])
        self.assertEqual(
            manifest["builds"],
            [{"chipFamily": "ESP32", "parts": [{"path": "wifi-onboarding-0.10.0-esp32-8mb.factory.bin", "offset": 0}]}],
        )
        # Images lose their blank tail
        self.assertEqual((self.dir / "0.9.0" / "wifi-onboarding-0.9.0-esp32-4mb.factory.bin").stat().st_size, 4096)

    def test_old_target_listed_by_id(self):
        self.add_factory("0.1.0", "esp32-2mb")
        target = self.build()[1]["versions"][0]["targets"][0]
        self.assertEqual((target["title"], target["description"], target["recommended"]), ("esp32-2mb", "", False))

    def test_repository_targets_have_text(self):
        self.add_factory("0.2.0", "esp32-4mb")
        index = release.build_install_index(self.dir, "wifi-onboarding")[1]
        self.assertTrue(index["versions"][0]["targets"][0]["title"])

    def test_rejects_mismatched_file(self):
        self.add_factory("0.2.0", "esp32-4mb")
        (self.dir / "0.2.0" / "wifi-onboarding-0.1.9-esp32-4mb.factory.bin").write_bytes(b"\xe9")
        with self.assertRaisesRegex(ValueError, "expected wifi-onboarding-0.2.0-<target>"):
            self.build()

    def test_rejects_bad_target(self):
        self.add_factory("0.2.0", "esp32h2-4mb")
        with self.assertRaisesRegex(ValueError, "no Wi-Fi"):
            self.build()

    def test_needs_installer_text(self):
        self.add_factory("0.2.0", "esp32-4mb-relay")
        with self.assertRaisesRegex(ValueError, "TARGET_TITLE"):
            self.build()

    def test_rejects_other_folders(self):
        (self.dir / "latest").mkdir()
        with self.assertRaisesRegex(ValueError, "version folder"):
            self.build()

    def test_needs_a_factory_image(self):
        (self.dir / "0.2.0").mkdir()
        with self.assertRaisesRegex(ValueError, "no .factory.bin"):
            self.build()

    def test_needs_a_version(self):
        with self.assertRaisesRegex(ValueError, "no version folders"):
            self.build()


if __name__ == "__main__":
    unittest.main()
