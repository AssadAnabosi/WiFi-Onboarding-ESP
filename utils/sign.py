"""Sign a firmware build into an OTA update file.

Output layout (one .ota.bin):
  [firmware][info block, 128 bytes][signature, 512 bytes]

The signature is ECDSA P-256 over SHA-256 of firmware + info block, DER-encoded
and zero-padded to 512 bytes, the layout the ESP32 core's Update library
(Update.installSignature) and its tools/bin_signing.py use.

Usage:
  python utils/sign.py --key KEY.pem --target esp32-4mb \\
      --firmware build/esp32.ino.bin --partitions build/esp32.ino.partitions.bin \\
      --out wifi-onboarding-0.1.0-esp32-4mb.ota.bin
  python utils/sign.py --verify FILE.ota.bin --pubkey esp32/OtaPublicKey.h
"""

import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "VERSION"
PROJECT_CONFIG = ROOT / "esp32" / "ProjectConfig.h"

MAGIC = b"WOTA"
FORMAT_VERSION = 1
INFO_SIZE = 128
SIGNATURE_SIZE = 512
TEXT_FIELD_SIZE = 32

# magic, format version, firmware length, version major/minor/patch, chip type,
# layout hash, target ID, project slug, reserved
INFO_STRUCT = struct.Struct("<4sHIHHHH32s32s32s14s")
assert INFO_STRUCT.size == INFO_SIZE

ESP_IMAGE_MAGIC = 0xE9
ESP_IMAGE_CHIP_ID_OFFSET = 12
PARTITION_TABLE_SIZE = 0xC00


def text_field(value, name):
    data = value.encode("ascii")
    if len(data) >= TEXT_FIELD_SIZE:
        raise ValueError(f"{name} must be shorter than {TEXT_FIELD_SIZE} bytes: {value!r}")
    return data.ljust(TEXT_FIELD_SIZE, b"\0")


def encode_info(firmware_length, version, chip_id, layout_hash, target, slug):
    """Build the 128-byte info block."""
    major, minor, patch = version
    if len(layout_hash) != 32:
        raise ValueError("layout hash must be 32 bytes")
    return INFO_STRUCT.pack(
        MAGIC,
        FORMAT_VERSION,
        firmware_length,
        major,
        minor,
        patch,
        chip_id,
        layout_hash,
        text_field(target, "target"),
        text_field(slug, "project slug"),
        b"\0" * 14,
    )


def decode_info(block):
    """Parse a 128-byte info block into a dict."""
    if len(block) != INFO_SIZE:
        raise ValueError("info block must be 128 bytes")
    magic, fmt, length, major, minor, patch, chip, layout, target, slug, _reserved = INFO_STRUCT.unpack(block)
    if magic != MAGIC:
        raise ValueError("not an update file (bad magic)")
    return {
        "format": fmt,
        "firmware_length": length,
        "version": (major, minor, patch),
        "chip_id": chip,
        "layout_hash": layout,
        "target": target.rstrip(b"\0").decode("ascii"),
        "slug": slug.rstrip(b"\0").decode("ascii"),
    }


def layout_hash(partitions_bin):
    """SHA-256 of the binary partition table as flashed at 0x8000 (padded to 0xC00 with 0xFF)."""
    data = bytes(partitions_bin)
    if len(data) > PARTITION_TABLE_SIZE:
        raise ValueError("partition table larger than 0xC00 bytes")
    return hashlib.sha256(data.ljust(PARTITION_TABLE_SIZE, b"\xff")).digest()


def chip_id_of(firmware):
    if len(firmware) < 16 or firmware[0] != ESP_IMAGE_MAGIC:
        raise ValueError("firmware is not an ESP app image (a factory image cannot be signed as an update)")
    return struct.unpack_from("<H", firmware, ESP_IMAGE_CHIP_ID_OFFSET)[0]


def read_version(version_file=VERSION_FILE):
    text = version_file.read_text(encoding="utf-8").strip()
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", text)
    if not match:
        raise ValueError(f"{version_file}: expected MAJOR.MINOR.PATCH")
    return tuple(int(part) for part in match.groups())


def read_project_slug(config=PROJECT_CONFIG):
    match = re.search(r'#define\s+PROJECT_SLUG\s+"([^"]+)"', config.read_text(encoding="utf-8"))
    if not match:
        raise ValueError(f"{config}: PROJECT_SLUG not found")
    return match.group(1)


def load_private_key(path):
    key = serialization.load_pem_private_key(Path(path).read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise ValueError(f"{path}: expected an ECDSA P-256 private key")
    return key


def sign(firmware, private_key, version, target, slug, partitions_bin):
    """Return the signed update file bytes."""
    info = encode_info(len(firmware), version, chip_id_of(firmware), layout_hash(partitions_bin), target, slug)
    signed = firmware + info
    signature = private_key.sign(signed, ec.ECDSA(hashes.SHA256()))
    return signed + signature.ljust(SIGNATURE_SIZE, b"\0")


def split(update):
    """Split an update file into (firmware, info dict, signed bytes, DER signature)."""
    if len(update) < INFO_SIZE + SIGNATURE_SIZE:
        raise ValueError("file too small to be an update")
    signed, padded = update[:-SIGNATURE_SIZE], update[-SIGNATURE_SIZE:]
    info = decode_info(signed[-INFO_SIZE:])
    firmware = signed[:-INFO_SIZE]
    if info["firmware_length"] != len(firmware):
        raise ValueError("firmware length in the info block does not match the file")
    # DER SEQUENCE header gives the real signature length
    if padded[0] != 0x30 or padded[1] >= 0x80:
        raise ValueError("signature is not DER-encoded")
    signature = padded[: 2 + padded[1]]
    return firmware, info, signed, signature


def verify(update, public_keys):
    """Return the index of the public key that signed the update, or None."""
    _firmware, _info, signed, signature = split(update)
    for index, key in enumerate(public_keys):
        try:
            key.verify(signature, signed, ec.ECDSA(hashes.SHA256()))
            return index
        except InvalidSignature:
            continue
    return None


def public_keys_from_header(header):
    """Read the DER public keys out of a generated OtaPublicKey header."""
    keys = []
    for body in re.findall(r"_DER\[\]\s*=\s*\{(.*?)\};", Path(header).read_text(encoding="utf-8"), re.S):
        der = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", body))
        keys.append(serialization.load_der_public_key(der))
    return keys


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--key", help="ECDSA P-256 private key (PEM)")
    parser.add_argument("--target", help="build target, e.g. esp32-4mb")
    parser.add_argument("--firmware", help="compiled app image (<sketch>.ino.bin)")
    parser.add_argument("--partitions", help="binary partition table (<sketch>.ino.partitions.bin)")
    parser.add_argument("--out", help="signed .ota.bin to write")
    parser.add_argument("--version", help="MAJOR.MINOR.PATCH (default: the VERSION file)")
    parser.add_argument("--slug", help="project slug (default: PROJECT_SLUG in ProjectConfig.h)")
    parser.add_argument("--verify", metavar="FILE", help="verify a signed update instead of signing")
    parser.add_argument("--pubkey", action="append", help="OtaPublicKey header or public .pem to verify against")
    args = parser.parse_args(argv)

    try:
        if args.verify:
            keys = []
            for source in args.pubkey or [str(ROOT / "esp32" / "OtaPublicKey.h")]:
                if source.endswith(".h"):
                    keys += public_keys_from_header(source)
                else:
                    keys.append(serialization.load_pem_public_key(Path(source).read_bytes()))
            update = Path(args.verify).read_bytes()
            info = split(update)[1]
            index = verify(update, keys)
            print(f"target {info['target']}, version {'.'.join(map(str, info['version']))}, project {info['slug']}")
            if index is None:
                print("signature: NOT valid for any of the given keys")
                return 1
            print(f"signature: valid (key {index})")
            return 0

        missing = [name for name in ("key", "target", "firmware", "partitions", "out") if not getattr(args, name)]
        if missing:
            parser.error("missing --" + ", --".join(missing))
        version = read_version() if not args.version else tuple(int(p) for p in args.version.split("."))
        slug = args.slug or read_project_slug()
        update = sign(
            Path(args.firmware).read_bytes(),
            load_private_key(args.key),
            version,
            args.target,
            slug,
            Path(args.partitions).read_bytes(),
        )
        Path(args.out).write_bytes(update)
        print(f"Signed {args.out}: {len(update)} bytes (target {args.target}, version {'.'.join(map(str, version))})")
        return 0
    except (ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
