# Contributing

Thanks for helping out. Build and flash instructions are in [docs/local-development.md](docs/local-development.md).

## Setting names

Settings are stored in NVS by name, and rollback to an older firmware reads the same names. To keep settings working across updates and rollbacks:

- **Never rename, reuse or change the type** of an existing setting name.
- **Every new setting has a default**, so a device without it keeps working with no migration.
- **Retired names stay listed as reserved** in `esp32/src/storage/ConfigStorage.cpp` and are never used again.
- Names are at most 15 characters (an NVS limit) and live in one list at the top of that file.

## Firmware code

The sketch's code lives in `esp32/src/`, one folder per module; [docs/architecture.md](docs/architecture.md#code-layout) says what goes where. Keep to the existing patterns:

- Modules are created in `esp32.ino` and get the modules they use as constructor references, not globals.
- Hardware and storage modules (`WiFiManager`, `FirmwareSlots`, `ConfigStorage`) don't know about HTTP; routes in `esp32/src/web/` turn their results into responses.
- Constants are `constexpr` in the `.cpp` that uses them; only `ProjectConfig.h` and build flags use `#define`.
- Log with `LOG("<module>", "...")`; never log passwords.
- Format with `esp32/.clang-format` (Arduino IDE: **Tools → Auto Format**). Don't format the generated headers.

## Generated files

`esp32/WebAssets.h`, `esp32/Version.h` and `esp32/OtaPublicKey.h` are generated. Never edit them by hand.

After changing anything in `web-interface/` or `VERSION`, run:

```sh
python utils/build_assets.py
```

and commit the source change and the regenerated files together. CI fails if they drift. Try page changes first with `python utils/ui_preview.py --mock` (or against a device without `--mock`); see [docs/local-development.md](docs/local-development.md#preview-the-web-interface).

## Dependencies

The ESP32 core and Arduino libraries are pinned to exact versions in `esp32/sketch.yaml`. Update them deliberately, in their own pull request, and make sure every target still builds.

## Commits

Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/).

## Versioning

Releases are cut by maintainers; see [docs/releasing.md](docs/releasing.md).
