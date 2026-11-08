# Contributing

Thanks for helping out. Build and flash instructions are in [docs/local-development.md](docs/local-development.md).

## Setting names

Settings are stored in NVS by name, and rollback to an older firmware reads the same names. To keep settings working across updates and rollbacks:

- **Never rename, reuse or change the type** of an existing setting name.
- **Every new setting has a default**, so a device without it keeps working with no migration.
- **Retired names stay listed as reserved** in `ConfigStorage` and are never used again.
- Names are at most 15 characters (an NVS limit) and live in one list in `ConfigStorage`.

## Generated files

`esp32/WebAssets.h`, `esp32/Version.h` and `esp32/OtaPublicKey.h` are generated. Never edit them by hand.

After changing anything in `web-interface/` or `VERSION`, run:

```sh
python utils/build_assets.py
```

and commit the source change and the regenerated files together. CI fails if they drift.

## Dependencies

The ESP32 core and Arduino libraries are pinned to exact versions in `esp32/sketch.yaml`. Update them deliberately, in their own pull request, and make sure every target still builds.

## Commits

Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/).

## Versioning

Releases are cut by maintainers; see [docs/releasing.md](docs/releasing.md).
