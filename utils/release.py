"""Release helpers used by the release workflow (.github/workflows/release.yml).

  python utils/release.py validate TAG [RELEASED_TAG ...]
                                        TAG matches VERSION, is newer than every released tag,
                                        and CHANGELOG.md has a section for it
  python utils/release.py check-repo OWNER/REPO
                                        OTA_GITHUB_OWNER/OTA_GITHUB_REPO in ProjectConfig.h name this repo
  python utils/release.py info          print version= and slug= lines (for $GITHUB_OUTPUT)
  python utils/release.py notes VERSION print the CHANGELOG.md section for VERSION
  python utils/release.py manifest DIR --repo OWNER/REPO --tag TAG
                                        write DIR/manifest.json from the signed .ota.bin files in DIR,
                                        then DIR/SHA256SUMS over every file in DIR
  python utils/release.py install DIR --tag TAG
                                        write an ESP Web Tools manifest (install-<target>.json) for each
                                        .factory.bin in DIR, and install-targets.json listing them
                                        (the browser installer, .github/workflows/pages.yml)
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sign import PROJECT_CONFIG, VERSION_FILE, read_project_slug, read_version, split  # noqa: E402
from targets import validate_name  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CHANGELOG = ROOT / "CHANGELOG.md"

MANIFEST_SCHEMA = 1
FORK_HELP = "see docs/make-it-yours.md"
TAG_PATTERN = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")


def parse_tag(tag):
    """(major, minor, patch) of a vX.Y.Z tag, or None."""
    match = TAG_PATTERN.match(tag)
    return tuple(int(part) for part in match.groups()) if match else None


def changelog_section(version, changelog=CHANGELOG):
    """Text of the '## [X.Y.Z]' section, without its heading; None when missing."""
    lines = changelog.read_text(encoding="utf-8").splitlines()
    heading = re.compile(r"^## \[" + re.escape(version) + r"\](\s|$)")
    for start, line in enumerate(lines):
        if heading.match(line):
            break
    else:
        return None
    body = []
    for line in lines[start + 1 :]:
        # Next section, or the link definitions at the end of the file
        if line.startswith("## ") or re.match(r"^\[[^\]]+\]:\s", line):
            break
        body.append(line)
    return "\n".join(body).strip()


def validate(tag, released_tags, version_file=VERSION_FILE, changelog=CHANGELOG):
    """Return a list of problems with releasing TAG (empty when it can be released)."""
    version = read_version(version_file)
    version_text = ".".join(map(str, version))
    problems = []
    wanted = parse_tag(tag)
    if wanted is None:
        return [f"tag {tag}: expected vMAJOR.MINOR.PATCH"]
    if wanted != version:
        problems.append(f"tag {tag} does not match VERSION ({version_text}): bump VERSION in a PR first")
    released = [v for v in (parse_tag(t) for t in released_tags if t != tag) if v is not None]
    if released and max(released) >= wanted:
        latest = ".".join(map(str, max(released)))
        problems.append(f"tag {tag} is not newer than the last release (v{latest})")
    section = changelog_section(version_text, changelog)
    if section is None:
        problems.append(f"CHANGELOG.md has no '## [{version_text}]' section")
    elif not section:
        problems.append(f"CHANGELOG.md section [{version_text}] is empty")
    return problems


def read_github_repo(config=PROJECT_CONFIG):
    text = config.read_text(encoding="utf-8")
    values = []
    for name in ("OTA_GITHUB_OWNER", "OTA_GITHUB_REPO"):
        match = re.search(r"#define\s+" + name + r'\s+"([^"]+)"', text)
        if not match:
            raise ValueError(f"{config.name}: {name} not found")
        values.append(match.group(1))
    return "/".join(values)


def check_repo(repository, config=PROJECT_CONFIG):
    """Return a list of problems (empty when ProjectConfig.h names this repository)."""
    configured = read_github_repo(config)
    # GitHub owner and repository names are case-insensitive
    if configured.lower() != repository.lower():
        return [
            f"ProjectConfig.h pulls updates from {configured}, but this is {repository}: "
            f"set OTA_GITHUB_OWNER and OTA_GITHUB_REPO ({FORK_HELP})"
        ]
    return []


def sha256_of(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def build_manifest(directory, repository, tag, slug, notes):
    """Manifest dict for the .ota.bin files in DIRECTORY."""
    version = parse_tag(tag)
    if version is None:
        raise ValueError(f"tag {tag}: expected vMAJOR.MINOR.PATCH")
    targets = {}
    for path in sorted(Path(directory).glob("*.ota.bin"), key=lambda p: p.name):
        _firmware, info, _signed, _signature = split(path.read_bytes())
        if info["version"] != version:
            raise ValueError(f"{path.name}: version {'.'.join(map(str, info['version']))} does not match {tag}")
        if info["slug"] != slug:
            raise ValueError(f"{path.name}: project {info['slug']} does not match {slug}")
        if info["target"] in targets:
            raise ValueError(f"{path.name}: second file for target {info['target']}")
        targets[info["target"]] = {
            "url": f"https://github.com/{repository}/releases/download/{tag}/{path.name}",
            "size": path.stat().st_size,
            "sha256": sha256_of(path),
        }
    if not targets:
        raise ValueError(f"{directory}: no .ota.bin files")
    return {
        "schema": MANIFEST_SCHEMA,
        "project": slug,
        "version": ".".join(map(str, version)),
        "notes": notes,
        "targets": targets,
    }


def write_manifest(directory, repository, tag, slug, notes):
    directory = Path(directory)
    manifest = build_manifest(directory, repository, tag, slug, notes)
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    files = sorted((path for path in directory.iterdir() if path.is_file() and path.name != "SHA256SUMS"), key=lambda p: p.name)
    sums = [f"{sha256_of(path)}  {path.name}" for path in files]
    (directory / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8", newline="\n")
    return manifest


def chip_family(target):
    """ESP Web Tools chip family of a target: esp32-4mb -> ESP32, esp32s3-8mb -> ESP32-S3."""
    chip = target.split("-")[0].upper()
    return chip if len(chip) <= 5 else f"{chip[:5]}-{chip[5:]}"


def build_install_manifests(directory, tag, slug):
    """{file name: ESP Web Tools manifest} for the .factory.bin files in DIRECTORY, plus install-targets.json."""
    version = parse_tag(tag)
    if version is None:
        raise ValueError(f"tag {tag}: expected vMAJOR.MINOR.PATCH")
    version_text = ".".join(map(str, version))
    pattern = re.compile(re.escape(f"{slug}-{version_text}-") + r"(.+)\.factory\.bin$")
    manifests = {}
    targets = []
    for path in sorted(Path(directory).glob("*.factory.bin"), key=lambda p: p.name):
        match = pattern.match(path.name)
        if not match:
            raise ValueError(f"{path.name}: expected {slug}-{version_text}-<target>.factory.bin")
        target = match.group(1)
        problems = validate_name(target)
        if problems:
            raise ValueError("; ".join(problems))
        name = f"install-{target}.json"
        manifests[name] = {
            "name": slug,
            "version": version_text,
            # The factory image covers the whole flash, settings included, so erasing first adds nothing
            "new_install_prompt_erase": False,
            "builds": [{"chipFamily": chip_family(target), "parts": [{"path": path.name, "offset": 0}]}],
        }
        targets.append({"target": target, "manifest": name})
    if not targets:
        raise ValueError(f"{directory}: no .factory.bin files")
    manifests["install-targets.json"] = {"project": slug, "version": version_text, "targets": targets}
    return manifests


def write_install_manifests(directory, tag, slug):
    manifests = build_install_manifests(directory, tag, slug)
    for name, manifest in manifests.items():
        (Path(directory) / name).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    return manifests


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    validate_parser = commands.add_parser("validate")
    validate_parser.add_argument("tag")
    validate_parser.add_argument("released", nargs="*")
    repo_parser = commands.add_parser("check-repo")
    repo_parser.add_argument("repository")
    commands.add_parser("info")
    notes_parser = commands.add_parser("notes")
    notes_parser.add_argument("version")
    manifest_parser = commands.add_parser("manifest")
    manifest_parser.add_argument("directory")
    manifest_parser.add_argument("--repo", required=True)
    manifest_parser.add_argument("--tag", required=True)
    install_parser = commands.add_parser("install")
    install_parser.add_argument("directory")
    install_parser.add_argument("--tag", required=True)
    args = parser.parse_args(argv)

    try:
        if args.command in ("validate", "check-repo"):
            problems = validate(args.tag, args.released) if args.command == "validate" else check_repo(args.repository)
            for problem in problems:
                print(f"::error::{problem}")
            if problems:
                return 1
            print("Release checks OK" if args.command == "validate" else "Repository OK")
        elif args.command == "info":
            print(f"version={'.'.join(map(str, read_version()))}")
            print(f"slug={read_project_slug()}")
        elif args.command == "notes":
            section = changelog_section(args.version)
            if section is None:
                raise ValueError(f"CHANGELOG.md has no '## [{args.version}]' section")
            print(section)
        elif args.command == "manifest":
            version = ".".join(map(str, read_version()))
            manifest = write_manifest(args.directory, args.repo, args.tag, read_project_slug(), changelog_section(version) or "")
            print(f"Manifest v{manifest['version']}: {', '.join(manifest['targets'])}")
        elif args.command == "install":
            manifests = write_install_manifests(args.directory, args.tag, read_project_slug())
            print(f"Install manifests: {', '.join(t['target'] for t in manifests['install-targets.json']['targets'])}")
    except (ValueError, OSError) as error:
        print(f"::error::{error}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
