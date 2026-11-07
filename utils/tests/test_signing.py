import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import keygen  # noqa: E402
import sign  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec  # noqa: E402
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature  # noqa: E402


def fake_firmware(size=4096, chip_id=0x0000):
    header = bytearray(24)
    header[0] = sign.ESP_IMAGE_MAGIC
    struct.pack_into("<H", header, sign.ESP_IMAGE_CHIP_ID_OFFSET, chip_id)
    return bytes(header) + bytes((i * 7) & 0xFF for i in range(size - len(header)))


PARTITIONS = b"\xaa\x50" + b"\x11" * 94


class SigningTest(unittest.TestCase):
    def setUp(self):
        self.key = ec.generate_private_key(ec.SECP256R1())
        self.firmware = fake_firmware()
        self.update = sign.sign(self.firmware, self.key, (1, 2, 3), "esp32-4mb", "wifi-onboarding", PARTITIONS)

    def test_layout_matches_core_bin_signing(self):
        # [firmware][info 128][DER signature zero-padded to 512]
        self.assertEqual(len(self.update), len(self.firmware) + 128 + 512)
        self.assertEqual(self.update[: len(self.firmware)], self.firmware)
        padded = self.update[-512:]
        der = padded[: 2 + padded[1]]
        decode_dss_signature(der)  # raises if not DER
        self.assertEqual(padded[len(der) :], b"\0" * (512 - len(der)))

    def test_round_trip_verifies(self):
        self.assertEqual(sign.verify(self.update, [self.key.public_key()]), 0)

    def test_other_key_is_rejected(self):
        other = ec.generate_private_key(ec.SECP256R1()).public_key()
        self.assertIsNone(sign.verify(self.update, [other]))
        self.assertEqual(sign.verify(self.update, [other, self.key.public_key()]), 1)

    def test_tampered_firmware_fails(self):
        tampered = bytearray(self.update)
        tampered[100] ^= 0x01
        self.assertIsNone(sign.verify(bytes(tampered), [self.key.public_key()]))

    def test_tampered_info_block_fails(self):
        tampered = bytearray(self.update)
        offset = len(self.firmware) + 10  # inside the version field
        tampered[offset] ^= 0x01
        self.assertIsNone(sign.verify(bytes(tampered), [self.key.public_key()]))

    def test_info_block_fields(self):
        info = sign.split(self.update)[1]
        self.assertEqual(info["format"], 1)
        self.assertEqual(info["firmware_length"], len(self.firmware))
        self.assertEqual(info["version"], (1, 2, 3))
        self.assertEqual(info["chip_id"], 0)
        self.assertEqual(info["target"], "esp32-4mb")
        self.assertEqual(info["slug"], "wifi-onboarding")
        self.assertEqual(info["layout_hash"], sign.layout_hash(PARTITIONS))

    def test_info_block_layout(self):
        block = sign.encode_info(0x01020304, (5, 6, 7), 9, b"\x22" * 32, "t", "s")
        self.assertEqual(len(block), 128)
        self.assertEqual(block[0:4], b"WOTA")
        self.assertEqual(block[4:6], b"\x01\x00")
        self.assertEqual(block[6:10], b"\x04\x03\x02\x01")
        self.assertEqual(block[10:16], b"\x05\x00\x06\x00\x07\x00")
        self.assertEqual(block[16:18], b"\x09\x00")
        self.assertEqual(block[18:50], b"\x22" * 32)
        self.assertEqual(block[50:82], b"t".ljust(32, b"\0"))
        self.assertEqual(block[82:114], b"s".ljust(32, b"\0"))
        self.assertEqual(block[114:128], b"\0" * 14)

    def test_layout_hash_pads_like_flash(self):
        self.assertEqual(sign.layout_hash(PARTITIONS), sign.layout_hash(PARTITIONS + b"\xff" * 10))

    def test_factory_image_cannot_be_signed(self):
        with self.assertRaises(ValueError):
            sign.sign(b"\xff" * 4096, self.key, (1, 0, 0), "esp32-4mb", "wifi-onboarding", PARTITIONS)

    def test_long_target_is_rejected(self):
        with self.assertRaises(ValueError):
            sign.encode_info(1, (1, 0, 0), 0, b"\0" * 32, "x" * 32, "s")


class KeygenTest(unittest.TestCase):
    def test_header_round_trip(self):
        keys = [ec.generate_private_key(ec.SECP256R1()).public_key() for _ in range(2)]
        with tempfile.TemporaryDirectory() as tmp:
            header = Path(tmp) / "OtaPublicKey.h"
            keygen.write_release_header(keys, header)
            loaded = keygen.read_release_keys(header)
            self.assertEqual([keygen.key_id(k) for k in loaded], [keygen.key_id(k) for k in keys])
            text = header.read_text(encoding="utf-8")
            self.assertIn("#define OTA_RELEASE_KEY_COUNT 2", text)

    def test_empty_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            header = Path(tmp) / "OtaPublicKey.h"
            keygen.write_release_header([], header)
            self.assertEqual(keygen.read_release_keys(header), [])
            self.assertIn("#define OTA_RELEASE_KEY_COUNT 0", header.read_text(encoding="utf-8"))

    def test_dev_header_manifest_url(self):
        key = ec.generate_private_key(ec.SECP256R1()).public_key()
        with tempfile.TemporaryDirectory() as tmp:
            header = Path(tmp) / "OtaPublicKey.local.h"
            keygen.write_dev_header(key, header, "http://192.168.1.10:8000/manifest.json")
            self.assertEqual(keygen.read_dev_manifest_url(header), "http://192.168.1.10:8000/manifest.json")
            self.assertEqual(keygen.key_id(sign.public_keys_from_header(header)[0]), keygen.key_id(key))
            keygen.write_dev_header(key, header)
            self.assertEqual(keygen.read_dev_manifest_url(header), "")
            for bad in ["ftp://x/m.json", 'http://x/"m.json', "http://x/m json"]:
                with self.assertRaises(ValueError):
                    keygen.write_dev_header(key, header, bad)

    def test_add_key_ignores_duplicates(self):
        key = ec.generate_private_key(ec.SECP256R1()).public_key()
        self.assertEqual(len(keygen.add_key(keygen.add_key([], key), key)), 1)

    def test_private_key_file_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "k.pem"
            key = keygen.new_private_key(path)
            self.assertEqual(keygen.key_id(keygen.load_public_key(path)), keygen.key_id(key.public_key()))
            with self.assertRaises(ValueError):
                keygen.new_private_key(path)
            self.assertIsInstance(sign.load_private_key(path), ec.EllipticCurvePrivateKey)


if __name__ == "__main__":
    unittest.main()
