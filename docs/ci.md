# CI workflows

Four GitHub Actions workflows live in `.github/workflows/`, plus Dependabot. This page says what each one does, what starts it and what it needs. The release and installer steps are explained in [releasing.md](releasing.md); this page is the map.

## At a glance

| Workflow | File | Starts on | Runs | Secrets |
| -- | -- | -- | -- | -- |
| Checks | `checks.yml` | Pull requests; pushes to `main` | Tools, generated-file drift, target names, a build of every target | none |
| Docs | `docs.yml` | Pull requests and pushes to `main` that touch Markdown, `docs/`, or the formatter's files | Markdown formatting check | none |
| Release | `release.yml` | A pushed `v*` tag | Validate, build every target, sign, publish a pre-release | `OTA_SIGNING_KEY` (in the `release` Environment) |
| Installer | `pages.yml` | A release made public; by hand | Build and deploy the browser installer to GitHub Pages | none (`pages: write` permission) |

Dependabot (`.github/dependabot.yml`) opens weekly pull requests for the Python tools in `utils/` and for GitHub Actions versions. Those pull requests run Checks like any other.

## Checks

Runs `tools` first, then one `build` job per target.

- **Starts on:** every pull request, and pushes to `main`. Pushes to other branches don't start it, because their pull request already does; this also avoids two runs for the same commit. Pushing a tag doesn't start it either (the Release workflow does its own checks).

- **Concurrency:** one run per pull request; a new push cancels the older run. Runs on `main` are never cancelled.

- **`tools` ("Tools and generated files"):** Python tests, `build_assets.py` followed by a diff of `esp32/WebAssets.h` and `esp32/Version.h`, `utils/targets.py check`, then lists the targets for the build matrix.

- **`build` ("Build `<target>`"):** one job per `sketch.yaml` profile: `utils/targets.py prepare`, `arduino-cli compile`, then `utils/targets.py size` against the target's app slot (warns above 90%). The ESP32 core and libraries are cached by the hash of `sketch.yaml`.

- **Docs-only changes:** a "Scope" step in `tools` looks at the changed files. When they are all under `docs/`, `*.md`, `.mdformat.toml` or `utils/requirements-docs.txt`, every step after it is skipped. The jobs still run and report success, so the checks stay green.

  The jobs must not be skipped as a whole (for example with `paths-ignore` on the trigger): the `main` ruleset requires `Tools and generated files` and every `Build <target>` check, and a check that never runs is never reported, so the pull request could not merge.

  If the changed files can't be listed (a first push of a new branch, for instance), the full checks run.

- **Adding a target** needs no workflow change: the matrix is built from `sketch.yaml`. A new target must also be added to the required checks of the `main` ruleset, or its build won't block merging.

## Docs

- **Starts on:** pull requests and pushes to `main` that change `docs/**`, any `*.md`, `.mdformat.toml`, `utils/requirements-docs.txt` or the workflow itself. Other changes don't start it.
- **Runs:** `mdformat --check --compact-tables` over the root Markdown files and `docs/` (`THIRD_PARTY_LICENSES.md` is generated and left out), with the version pinned in `utils/requirements-docs.txt`.
- **Fix a failure** with the same command without `--check`; see [CONTRIBUTING.md](../CONTRIBUTING.md#docs-change-with-the-code).
- It isn't a required check, so a docs change that doesn't touch these paths is never held up by it.

## Release

- **Starts on:** pushing a tag that matches `v*`. Path filters don't apply to tags, so it always runs in full. See [releasing.md](releasing.md) for how to cut a release.
- **Concurrency:** one release at a time, never cancelled.
- **Jobs:**
  1. `validate`: tests, generated-file drift, target names, then `release.py validate` (tag equals `VERSION`, newer than every existing release, `CHANGELOG.md` has a `## [X.Y.Z]` section) and `release.py check-repo` (the owner and repo in `ProjectConfig.h` name this repository).
  2. `build`: one job per target, no secrets and no cache, with the commit hash in `build_opt.h`. Output is uploaded as an artifact kept for 7 days.
  3. `release` ("Sign and publish"): the only job in the protected `release` Environment, so it waits for approval and is the only one that can read `OTA_SIGNING_KEY`. It signs and verifies each target, writes `manifest.json` and `SHA256SUMS`, and publishes a **pre-release**.
- Making the pre-release public is a manual step; that starts the Installer workflow.

## Installer

- **Starts on:** a release being made public (`released`; a pre-release doesn't count), or by hand from Actions → Installer → Run workflow.
- **Concurrency:** one deployment at a time (group `pages`), never cancelled.
- **Jobs:** `build` downloads every public release's `.factory.bin` files (checked against `SHA256SUMS`) and notes, adds ESP Web Tools at the version pinned in `pages.yml`, the installer page and the demo of the web interface, then `deploy` publishes the site to GitHub Pages through the `github-pages` Environment.
- It does not run when `installer/` or `web-interface/` change; republish by hand after such a change reaches `main`.

## Repository settings the workflows rely on

| Setting | Why |
| -- | -- |
| `main` ruleset: pull request required, with the `Tools and generated files` and `Build <target>` checks | Merging needs Checks; see the docs-only note above |
| `release` tag ruleset | Release tags can't be deleted or moved |
| `release` Environment with the `OTA_SIGNING_KEY` secret and required reviewers | Only the signing job can read the key, and only after approval |
| `github-pages` Environment with a deployment branch policy | Deployment target for Installer; keep it limited to `main` |

## Changing the workflows

- Pin actions to exact versions (`actions/checkout@v7.0.1`); Dependabot proposes updates.
- A pull request runs its own copy of the workflows, so review changes under `.github/workflows/` as carefully as code (see [security.md](security.md)).
- Keep the Python and arduino-cli versions in the `env:` block of every workflow in step.
- Update this page in the same pull request as the workflow change.
