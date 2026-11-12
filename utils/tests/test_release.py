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


class InstallManifestTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def add_factory(self, name):
        (self.dir / name).write_bytes(b"\xff" * 64)

    def test_chip_family(self):
        self.assertEqual(release.chip_family("esp32-4mb"), "ESP32")
        self.assertEqual(release.chip_family("esp32s3-8mb-psram"), "ESP32-S3")
        self.assertEqual(release.chip_family("esp32c61-4mb"), "ESP32-C61")

    def test_manifests_per_target(self):
        self.add_factory("wifi-onboarding-0.2.0-esp32-4mb.factory.bin")
        self.add_factory("wifi-onboarding-0.2.0-esp32-8mb.factory.bin")
        self.add_factory("wifi-onboarding-0.2.0-esp32-4mb.ota.bin")  # Ignored
        release.write_install_manifests(self.dir, "v0.2.0", "wifi-onboarding")

        manifest = json.loads((self.dir / "install-esp32-8mb.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["name"], "wifi-onboarding")
        self.assertEqual(manifest["version"], "0.2.0")
        self.assertFalse(manifest["new_install_prompt_erase"])
        self.assertEqual(
            manifest["builds"],
            [{"chipFamily": "ESP32", "parts": [{"path": "wifi-onboarding-0.2.0-esp32-8mb.factory.bin", "offset": 0}]}],
        )
        index = json.loads((self.dir / "install-targets.json").read_text(encoding="utf-8"))
        self.assertEqual(index["version"], "0.2.0")
        self.assertEqual(
            index["targets"],
            [
                {"target": "esp32-4mb", "manifest": "install-esp32-4mb.json"},
                {"target": "esp32-8mb", "manifest": "install-esp32-8mb.json"},
            ],
        )

    def test_rejects_other_version(self):
        self.add_factory("wifi-onboarding-0.1.9-esp32-4mb.factory.bin")
        with self.assertRaisesRegex(ValueError, "expected wifi-onboarding-0.2.0-<target>"):
            release.build_install_manifests(self.dir, "v0.2.0", "wifi-onboarding")

    def test_rejects_bad_target(self):
        self.add_factory("wifi-onboarding-0.2.0-esp32h2-4mb.factory.bin")
        with self.assertRaisesRegex(ValueError, "no Wi-Fi"):
            release.build_install_manifests(self.dir, "v0.2.0", "wifi-onboarding")

    def test_needs_a_factory_image(self):
        with self.assertRaisesRegex(ValueError, "no .factory.bin"):
            release.build_install_manifests(self.dir, "v0.2.0", "wifi-onboarding")


if __name__ == "__main__":
    unittest.main()
