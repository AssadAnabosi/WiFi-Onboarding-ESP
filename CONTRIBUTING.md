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

## Branches, commits and pull requests

### Branches

Every branch name follows [Conventional Branch](https://conventional-branch.github.io/), with purpose prefixes only: `<type>/<description>`.

- **Allowed types:** `feature/` (or `feat/`), `bugfix/` (or `fix/`), `hotfix/` (urgent production fixes), `release/` (e.g. `release/v1.2.0`), `chore/` (dependencies, docs, tooling).
- **No AI agent source prefixes:** no `ai/`, `claude/`, `codex/`, `copilot/` or `cursor/` branches, even when an AI agent creates the branch. Use the purpose prefix for the work instead.
- **The description** uses lowercase `a-z`, digits, hyphens between words and dots only inside versions. No uppercase, underscores, spaces, consecutive hyphens or dots, and no leading or trailing hyphen or dot.
- **Include the GitHub issue number:** `feat/issue-42-undo-ready`, `fix/issue-57-sse-reconnect`, `chore/issue-31-branch-naming-convention`.
- `main` is the only branch without a prefix. The one other exception is `dependabot/*`, which Dependabot names itself.

### One issue, one branch, one PR

The PR body contains `Closes #<issue>`, so GitHub links it in the issue's Development sidebar and closes the issue on merge.

- **Open every PR against `main` first**, even when it builds on another open PR. GitHub registers the issue link only for PRs opened against the default branch.
- Work that builds on other open PRs is then linked into a GitHub stack with `gh stack`: `gh stack link <pr1> <pr2> …` (bottom to top), or `gh stack link <stack-number> <new-pr>` to add to an existing stack. The stack keeps the issue links. A PR created directly on a stacked branch never gets the link, and its base can't be changed once it's in a stack.
- The PR body states its place in the stack, for example `Part of stack #49. Merges after #45.`

### Commits

Every commit message follows [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/):

```
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
```

- **Types:** `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`.
- **Scope** is optional and names the area, e.g. `web`, `wifi`, `ota`, `storage`, `ui`, `installer`, `ci`, `deps`, `docs`, `release`.
- **The description** is imperative, lowercase and has no trailing period, e.g. `feat(ota): add manual rollback`.
- **Breaking changes** are marked with `!` after the type/scope or a `BREAKING CHANGE:` footer.
- **PR titles** follow the same format.

### No AI attribution

Never add an AI co-author or AI attribution. No `Co-Authored-By:` trailer naming an AI model or tool (Claude, Copilot, etc.), and no "Generated with …" lines in commit messages or PR descriptions. This applies to every commit, including AI-assisted ones.

## Docs change with the code

A change isn't complete until the docs match it, and they land in the same commit/PR as the code. Before opening a PR, check every row that applies:

| If you change… | Update |
| -- | -- |
| An HTTP route, request or response field (`esp32/src/web/`, `/api/status`) | [docs/api.md](docs/api.md) and the fake device `utils/ui_mock.js` |
| A setting (`ConfigStorage`, `/api/settings`, `settings.html`) | [docs/api.md](docs/api.md), `SETTING_KEYS` in `web-interface/js/settings.js`, `SETTINGS` in `utils/ui_mock.js` |
| Modules, folders or how they depend on each other | [docs/architecture.md](docs/architecture.md#code-layout) and `CLAUDE.md` |
| Defaults, GitHub owner/repo or `PROJECT_SLUG` (`ProjectConfig.h`) | [docs/make-it-yours.md](docs/make-it-yours.md) |
| Updates, rollback, signing, keys or the manifest | [docs/ota-updates.md](docs/ota-updates.md) and [docs/security.md](docs/security.md) |
| Release or installer workflows, versioning | [docs/releasing.md](docs/releasing.md) |
| Any workflow in `.github/workflows/` or its triggers | [docs/ci.md](docs/ci.md) |
| Build, flash, tooling, CI or the preview server | [docs/local-development.md](docs/local-development.md) and `CLAUDE.md` |
| Supported hardware, install options or first-run steps | [docs/getting-started.md](docs/getting-started.md) and `README.md` |
| Anything a user of the firmware or installer would notice | `CHANGELOG.md`, under `## [Unreleased]` |
| These contribution rules | `CLAUDE.md` working conventions |

Markdown is formatted with [mdformat](https://mdformat.readthedocs.io/) (pinned in `utils/requirements-docs.txt`, settings in `.mdformat.toml`); CI fails on unformatted files. Fix them with:

```sh
pip install -r utils/requirements-docs.txt
mdformat --compact-tables CHANGELOG.md CLAUDE.md CONTRIBUTING.md README.md SECURITY.md docs
```

`THIRD_PARTY_LICENSES.md` is generated and not formatted.

## Versioning

Releases are cut by maintainers; see [docs/releasing.md](docs/releasing.md).
