# Releasing

A release is a pushed `vX.Y.Z` tag. CI builds and signs it and publishes a pre-release, which you check and then make public.

## Versioning

- The version lives in one place: the `VERSION` file at the repository root. The firmware (`esp32/Version.h`), the web interface footer, the signed files, the tag and the manifest all take it from there.
- Format: `MAJOR.MINOR.PATCH` ([Semantic Versioning](https://semver.org/)), with no pre-release suffixes.
- `CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/). Every release has a `## [X.Y.Z] - YYYY-MM-DD` section. That section becomes the release notes on GitHub and on the device.

## Cutting a release

1. Open a pull request that:
   - sets `VERSION` to the new version,
   - moves the `[Unreleased]` entries into a new `## [X.Y.Z] - YYYY-MM-DD` section of `CHANGELOG.md`,
   - includes the regenerated files from `python utils/build_assets.py` (the version is written into `Version.h` and the pages).
2. Merge it once CI passes.
3. Run the [scratch-repo pull test](#scratch-repo-pull-test) and the [manual hardware checklist](#manual-hardware-checklist).
4. Tag the merge commit and push the tag:

   ```sh
   git tag -a vX.Y.Z -m vX.Y.Z
   git push origin vX.Y.Z
   ```

5. Approve the deployment when the workflow asks (see [The pipeline](#the-pipeline)), then [publish](#publishing).

## The pipeline

Two GitHub Actions workflows in `.github/workflows/`:

**`checks.yml`**: every push and pull request, no secrets.

1. Python tests (`utils/tests`).
2. Generated files are current: reruns `build_assets.py` and fails on any difference.
3. Target names are valid (`utils/targets.py check`).
4. Every target in `esp32/sketch.yaml` is compiled in its own job, and its firmware size is checked against its app slot (warning above 90%).

**`release.yml`**: on a pushed `v*` tag.

1. **Validate** (no secrets): the same tests and checks, then:
   - the tag matches `VERSION`,
   - the version is newer than every existing GitHub release,
   - `CHANGELOG.md` has a non-empty section for it,
   - `OTA_GITHUB_OWNER`/`OTA_GITHUB_REPO` in `ProjectConfig.h` name this repository.
2. **Build** (no secrets, no cache): each target in its own job, with the commit hash compiled in. Uploads the app, the partition table and the 4 MB merged image.
3. **Sign and publish**: the only job that runs in the protected `release` Environment, so it waits for your approval (**Actions → the run → Review deployments → release → Approve and deploy**). It:
   - writes `OTA_SIGNING_KEY` to a temporary file and checks that `esp32/OtaPublicKey.h` trusts it,
   - signs every target and verifies each signature,
   - deletes the key file, even when a step fails,
   - writes `manifest.json` (release notes = the changelog section) and `SHA256SUMS`,
   - creates a GitHub **pre-release** with the assets.

Release assets:

| File | Purpose |
| --- | --- |
| `wifi-onboarding-<version>-<target>.ota.bin` | Signed update, for manual upload and update checks |
| `wifi-onboarding-<version>-<target>.factory.bin` | Full 4 MB image for a first USB flash (wipes everything) |
| `manifest.json` | Read by devices when they check for updates |
| `SHA256SUMS` | Checksums of all the files above |
| `THIRD_PARTY_LICENSES.md` | Licenses of the bundled components |

The `<project>` prefix of the file names is `PROJECT_SLUG` from `ProjectConfig.h`.

## Scratch-repo pull test

Before each release, test the real GitHub path (HTTPS, the download redirect, "latest") on a scratch repository rather than the real one. The real repository never gets test releases.

1. Create a scratch repository (e.g. `ota-test`) with an Environment `release` holding a **dev** key as `OTA_SIGNING_KEY`.
2. On a local branch, set `OTA_GITHUB_REPO` to the scratch repository and make the dev key the only trusted key:

   ```sh
   python utils/keygen.py --add-public keys/ota-dev.pem
   python utils/keygen.py --remove <release key ID>   # IDs: python utils/keygen.py --list
   ```

3. Push that branch to the scratch repository and tag two versions, e.g. `v0.1.0`, then a higher one with a visible change. Make each release public after its run.
4. USB-flash the first version's `.factory.bin`, connect the device to Wi-Fi, click **Check for updates**, then **Install**. Check that the release notes show as text, the update installs, and the footer shows the new version.

## Publishing

The workflow publishes a **pre-release**. Devices never see it, because "latest" skips pre-releases. Check it before going public:

1. Open the release and check that every asset is there.
2. Download the files and run `sha256sum -c SHA256SUMS`.
3. Optionally verify the signature: `python utils/sign.py --verify <file>.ota.bin`.
4. Edit the release, untick **Set as a pre-release**, tick **Set as the latest release**, and save. Devices see it at their next check.

## Browser installer

`.github/workflows/pages.yml` publishes the [browser installer](https://assadanabosi.github.io/WiFi-Onboarding-ESP/) to GitHub Pages when a release is made public (the `released` event; pre-releases never reach it), and on demand (Actions → Installer → Run workflow). The page offers every public release, newest first and selected by default, with its release notes, plus a file of the user's own. The workflow:

1. downloads each public release's `.factory.bin` files into `firmware/<version>/`, checks them against that release's `SHA256SUMS`, and saves the release body (its CHANGELOG section) as the version's notes;
2. runs `python utils/release.py install site/firmware`, which trims each image's blank (0xFF) tail, writes one [ESP Web Tools](https://esphome.github.io/esp-web-tools/) manifest per image (`<version>/install-<target>.json`) and the index the page reads (`install-index.json`: versions newest first, their notes, and their targets in `sketch.yaml` order with each target's `TARGET_TITLE` and `TARGET_DESCRIPTION` and the default profile marked recommended; a target that is no longer in `sketch.yaml` is listed by its ID);
3. adds ESP Web Tools itself from npm (`npm pack`, version pinned in `ESP_WEB_TOOLS_VERSION`; npm checks the package integrity);
4. adds the page (`installer/`) with the device's `css/style.css`, `js/app.js`, `js/markdown.js` and `favicon.ico` from `web-interface/`, so it has the same look, header, theme switch and release-notes rendering as the device.

The firmware files are served from Pages, next to the page, because a browser can't fetch GitHub release downloads cross-origin. Trimming takes each image from the full flash size (4 or 8 MB) to about 1.4 MB, so every release adds only a few MB to the site (GitHub Pages allows about 1 GB). It is safe because the page always installs from scratch, and ESP Web Tools then erases the whole chip before writing. The release assets themselves are not changed.

A file of the user's own is checked in the browser only for being a full flash image (bootloader and partition table present; the chip is read from the bootloader header); `.ota.bin` and app-only files are refused with a hint. It is not signed or checked otherwise, and the page says so.

To try the page locally, lay out a folder the same way (`index.html`, `installer.css`, `installer.js`, `css/`, `js/`, `esp-web-tools/` from `npm pack`, and `firmware/<version>/` folders with `.factory.bin` files and an optional `NOTES.md`, then `release.py install` on `firmware/`) and run `python -m http.server` in it (`localhost` counts as secure for Web Serial).

## Bad releases

Devices only ever move forward through update checks. If a release is broken:

1. Edit it and mark it as a **pre-release** again. "Latest" falls back to the previous release, so no more devices pick it up. Then run the Installer workflow by hand so the browser installer drops it too.
2. Fix the problem and release it as a **higher** version.

Devices that already installed the broken version stay on it until the fixed version arrives; an update check never downgrades. If the broken firmware can't stay up, devices roll back on their own (see [ota-updates.md](ota-updates.md#rollback)).

## GitHub setup

Done once per repository (forks too; see [make-it-yours.md](make-it-yours.md)):

- **Environment `release`** (Settings → Environments):
  - secret `OTA_SIGNING_KEY` = the full contents of the private release key `.pem`;
  - you as **required reviewer**;
  - deployments restricted to tags matching `v[0-9]*.[0-9]*.[0-9]*`.

  Keep no repository-level copy of the key, and keep an offline backup.
- **GitHub Pages** (Settings → Pages): source **GitHub Actions**. Then in Settings → Environments → `github-pages`, add the deployment tag rule `v[0-9]*.[0-9]*.[0-9]*`: a run started by a release runs on its tag, which that environment refuses by default.
- **Tag ruleset** (Settings → Rules → Rulesets): target tags matching `v[0-9]*.[0-9]*.[0-9]*`; restrict creation, update and deletion, with only you able to bypass. Only you can start a release.
- **`main` ruleset** (Settings → Rules → Rulesets): target the default branch; block deletion and force pushes; require a pull request; require these status checks from GitHub Actions: `Tools and generated files` and one `Build <target>` per target in `esp32/sketch.yaml` (today `Build esp32-4mb`, `Build esp32-8mb` and `Build esp32-8mb-psram`); only you can bypass. This makes the generated-file check (which guards the unreadable `esp32/WebAssets.h`) and every build pass before anything is merged.

  Required checks are listed by name. **When you add, rename or remove a target, update this list too**, or the new target's build isn't required (and a removed one blocks every merge, since it never reports).
- **Private vulnerability reporting** (Settings → Security): enabled, as `SECURITY.md` asks reporters to use it.

## Manual hardware checklist

CI covers the tools and builds. Device behaviour is checked by hand on a real ESP32 before every release:

- [ ] Fresh USB flash of `.factory.bin`: everything wiped, onboarding works, home target set
- [ ] Browser installer (after making the release public): installs, sends the Wi-Fi network over Improv, **Visit device** opens the web interface
- [ ] Installer on a device that is already connected: detected over Improv, **Change Wi-Fi** works
- [ ] Manual upload of a newer signed file: installs, settings kept, footer shows the new version
- [ ] Power pulled mid-upload: old firmware boots, settings kept
- [ ] Browser closed mid-upload: upload cancelled, a new upload can start
- [ ] Unsigned file, untrusted-key file and another project's file: blocked with the right message
- [ ] Other chip's file and a `.factory.bin`: blocked with the right message
- [ ] Other configuration's file: warning, installs, persistent mismatch warning appears
- [ ] Home target file uploaded after a mismatch: warning clears
- [ ] Downgrade by manual upload: warning, installs
- [ ] Reboot, settings save and factory reset during the unconfirmed window: refused with the countdown
- [ ] Reset during the unconfirmed window: rolls back to the previous version
- [ ] Firmware that never becomes healthy: rolls back at the deadline, rollback button hidden
- [ ] Firmware that crashes on boot: rolls back on the first reset
- [ ] Manual rollback: returns to the previous version, button hidden afterwards
- [ ] Pull from the scratch repo: update found, release notes shown as text, install works
- [ ] Pull in AP-only mode: clear "no internet" error
- [ ] Network dropped mid-download: old firmware keeps running
- [ ] Dev build: shows "(dev)", accepts dev-signed and release-signed files
- [ ] Factory reset: settings and driver Wi-Fi credentials cleared; firmware, home target and slot records kept
- [ ] Cross-site request from another page (plain form POST): rejected
- [ ] Browser after an update: no stale UI (page ETag and 304 work, CSS/JS load with the new `?v=` hash)
- [ ] Web UI on a phone and a desktop, light and dark theme: Home, Networks (connect, hidden network, enterprise), Settings (save, password eye)
