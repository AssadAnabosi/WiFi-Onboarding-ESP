"""Build target helpers used by CI.

A target is a profile in esp32/sketch.yaml named <chip>-<flash>[-<config>].

  python utils/targets.py check             validate target names and their files
  python utils/targets.py list              print the targets as a JSON list
  python utils/targets.py prepare TARGET [--commit SHA]
                                            put TARGET's partitions file and build_opt.h in place
  python utils/targets.py size TARGET BIN   check a firmware .bin against TARGET's app slot
"""

import argparse
import csv
import json
import re
import shutil
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
SKETCH_DIR = ROOT / "esp32"
SKETCH_YAML = SKETCH_DIR / "sketch.yaml"

# Espressif target names of chips with Wi-Fi
WIFI_CHIPS = ("esp32", "esp32s2", "esp32s3", "esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32c61")
# Chips without Wi-Fi: never supported
NO_WIFI_CHIPS = ("esp32h2", "esp32h4", "esp32p4")
FLASH_SIZES = ("2mb", "4mb", "8mb", "16mb", "32mb")
NAME_PATTERN = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")

SIZE_WARN_RATIO = 0.90


def validate_name(name):
    """Return a list of problems with a target name (empty when valid)."""
    if not NAME_PATTERN.match(name):
        return [f"{name}: use lowercase letters, digits and single '-' only"]
    parts = name.split("-")
    if len(parts) < 2:
        return [f"{name}: expected <chip>-<flash>[-<config>]"]
    chip, flash = parts[0], parts[1]
    problems = []
    if chip in NO_WIFI_CHIPS:
        problems.append(f"{name}: {chip} has no Wi-Fi and cannot be supported")
    elif chip not in WIFI_CHIPS:
        problems.append(f"{name}: unknown chip '{chip}' (expected one of {', '.join(WIFI_CHIPS)})")
    if flash not in FLASH_SIZES:
        problems.append(f"{name}: unknown flash size '{flash}' (expected one of {', '.join(FLASH_SIZES)})")
    return problems


def load_sketch(sketch_yaml=SKETCH_YAML):
    data = yaml.safe_load(sketch_yaml.read_text(encoding="utf-8")) or {}
    profiles = data.get("profiles") or {}
    return profiles, data.get("default_profile")


def partitions_file(target, sketch_dir=SKETCH_DIR):
    """A target's own partition table. esp32/partitions.csv is only the copy the build reads."""
    return sketch_dir / "targets" / f"{target}.partitions.csv"


def read_text(path):
    return path.read_text(encoding="utf-8").replace("\r\n", "\n")


def build_opt(target, commit=None):
    lines = [
        "-DUPDATE_SIGN",
        f'-DOTA_TARGET_ID=\\"{target}\\"',
        f'-DOTA_TARGET_HEADER=\\"targets/{target}.h\\"',
    ]
    if commit:
        lines.append(f'-DFIRMWARE_COMMIT=\\"{commit}\\"')
    return "\n".join(lines) + "\n"


def parse_size(value):
    value = value.strip().upper()
    if value.endswith("K"):
        return int(value[:-1], 0) * 1024
    if value.endswith("M"):
        return int(value[:-1], 0) * 1024 * 1024
    return int(value, 0)


def app_slot_size(partitions_csv):
    """Size in bytes of the first OTA app slot in a partition table."""
    for row in csv.reader(partitions_csv.read_text(encoding="utf-8").splitlines()):
        if not row or row[0].strip().startswith("#"):
            continue
        cells = [cell.strip() for cell in row]
        if len(cells) >= 5 and cells[1] == "app" and cells[2] == "ota_0":
            return parse_size(cells[4])
    raise ValueError(f"{partitions_csv}: no ota_0 app partition")


def check(sketch_dir=SKETCH_DIR):
    profiles, default_target = load_sketch(sketch_dir / "sketch.yaml")
    problems = []
    if not profiles:
        problems.append("sketch.yaml: no profiles")
    if default_target not in profiles:
        problems.append(f"sketch.yaml: default_profile '{default_target}' is not a profile")
    for target in profiles:
        problems += validate_name(target)
        header = sketch_dir / "targets" / f"{target}.h"
        if not header.exists():
            problems.append(f"{target}: missing {header.relative_to(sketch_dir.parent).as_posix()}")
        partitions = partitions_file(target, sketch_dir)
        if not partitions.exists():
            problems.append(f"{target}: missing {partitions.relative_to(sketch_dir.parent).as_posix()}")
    if default_target in profiles:
        if read_text(sketch_dir / "build_opt.h") != build_opt(default_target):
            problems.append(f"esp32/build_opt.h does not match the default target '{default_target}'")
        source = partitions_file(default_target, sketch_dir)
        if source.exists() and read_text(sketch_dir / "partitions.csv") != read_text(source):
            problems.append(f"esp32/partitions.csv does not match the default target '{default_target}'")
    return problems


def prepare(target, commit=None, sketch_dir=SKETCH_DIR):
    profiles, _default_target = load_sketch(sketch_dir / "sketch.yaml")
    if target not in profiles:
        raise ValueError(f"unknown target '{target}'")
    shutil.copyfile(partitions_file(target, sketch_dir), sketch_dir / "partitions.csv")
    (sketch_dir / "build_opt.h").write_text(build_opt(target, commit), encoding="utf-8", newline="\n")


def check_size(target, binary, sketch_dir=SKETCH_DIR):
    """Return (level, message): level is 'ok', 'warning' or 'error'."""
    slot = app_slot_size(partitions_file(target, sketch_dir))
    size = Path(binary).stat().st_size
    ratio = size / slot
    message = f"{target}: firmware {size} bytes, app slot {slot} bytes ({ratio:.1%})"
    if size > slot:
        return "error", message + " - too large for its slot"
    if ratio > SIZE_WARN_RATIO:
        return "warning", message + f" - above {SIZE_WARN_RATIO:.0%}"
    return "ok", message


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check")
    commands.add_parser("list")
    prepare_parser = commands.add_parser("prepare")
    prepare_parser.add_argument("target")
    prepare_parser.add_argument("--commit")
    size_parser = commands.add_parser("size")
    size_parser.add_argument("target")
    size_parser.add_argument("binary")
    args = parser.parse_args(argv)

    if args.command == "check":
        problems = check()
        for problem in problems:
            print(f"::error::{problem}")
        if problems:
            return 1
        print("Targets OK")
    elif args.command == "list":
        print(json.dumps(sorted(load_sketch()[0])))
    elif args.command == "prepare":
        prepare(args.target, args.commit)
        print(f"Prepared {args.target}")
    elif args.command == "size":
        level, message = check_size(args.target, args.binary)
        print(f"::{level}::{message}" if level != "ok" else message)
        return 1 if level == "error" else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
