# Make it yours

Checklist for turning a fork into your own project, with its own releases and its own signing key. Almost everything a fork changes lives in one file, `esp32/ProjectConfig.h`.

## Checklist

1. **Enable GitHub Actions** on the fork. Forks have them turned off by default (Actions tab → enable).

2. **Name the project** in `esp32/ProjectConfig.h`:

   | Setting | Meaning |
   | --- | --- |
   | `PROJECT_SLUG` | Short project ID, e.g. `my-sensor`. Written into every signed file and used in the release file names. Devices refuse files with another slug |
   | `OTA_GITHUB_OWNER`, `OTA_GITHUB_REPO` | The repository whose releases devices check for updates. Compiled in, so a device follows the repository its firmware was built for |

3. **Make your own signing key:**

   ```sh
   python utils/keygen.py
   python utils/keygen.py --remove <upstream key ID>   # IDs: python utils/keygen.py --list
   ```

   The first command writes the private key to `keys/ota-release.pem` (gitignored) and adds its public key to `esp32/OtaPublicKey.h`. The second stops trusting the upstream key. Commit `OtaPublicKey.h`, **never** the `.pem`. Back the private key up offline: losing it means devices can only be updated over USB.

4. **Set up GitHub** as in [releasing.md](releasing.md#github-setup): an Environment `release` with the private key as secret `OTA_SIGNING_KEY` and you as required reviewer, and a tag ruleset for `v*` tags.

5. **Set device defaults** in `ProjectConfig.h`: access point SSID and password (`DEFAULT_AP_SSID`, `DEFAULT_AP_PASSWORD`), channel, hostname (`DEFAULT_HOSTNAME`), the setup address (`MDNS_SETUP_NAME`, giving `<name>.local`) and the update check timing (`OTA_CHECK_*`).

6. **Start your own history:** set `VERSION` to `0.1.0`, empty `CHANGELOG.md` down to its header and an `[Unreleased]` section, then run `python utils/build_assets.py`.

7. **Branding:** page titles and footer in `web-interface/*.html`, `web-interface/favicon.ico`, and the README. Keep the MIT license notice and `THIRD_PARTY_LICENSES.md`.

8. **Confirm the board and flash layout** before your first release. The partition table (`esp32/partitions.csv`) and bootloader are never changed by an update, so a layout change after release means every device needs a USB flash. To support other boards, add build targets (see [architecture.md](architecture.md#build-targets)).

9. **Optional: allow configuration switches over OTA.** By default a device keeps its *home target* (the configuration it was USB-flashed with) and warns when it runs another configuration's file. To make an installed configuration the new home target instead, call `ConfigStorage::setHomeTarget(OTA_TARGET_ID);` in `UpdateManager::loop()`, where the firmware confirms itself (next to `esp_ota_mark_app_valid_cancel_rollback()`).

10. **Release `v0.1.0`:** push the tag, approve the deployment, check the pre-release assets and make it public ([releasing.md](releasing.md)).

## If you skip a step

| Skipped step | What happens | Caught by |
| --- | --- | --- |
| Actions not enabled | No checks and no releases run | Nothing: the Actions tab stays empty |
| `OTA_GITHUB_OWNER`/`OTA_GITHUB_REPO` not changed | Your devices would check the upstream repository | Release workflow, Validate job: "ProjectConfig.h pulls updates from …, but this is …" |
| No `OTA_SIGNING_KEY` in the `release` Environment | Nothing can be signed | Release workflow: "The release Environment has no OTA_SIGNING_KEY secret" |
| Key not in `OtaPublicKey.h` (e.g. upstream key kept) | Your devices would refuse your own releases | Release workflow: "OTA_SIGNING_KEY is not in esp32/OtaPublicKey.h" |
| Upstream key still trusted | Upstream releases with your `PROJECT_SLUG` could install on your devices; with a different slug they are refused | Nothing: remove it with `keygen.py --remove` |
| `PROJECT_SLUG` not changed | Your files and upstream files can't be told apart by slug | Nothing |
| `VERSION`/`CHANGELOG.md` not reset | Your first release has upstream's version number | Release workflow: tag must match `VERSION` and have a changelog section |
| Default AP password kept | Anyone nearby can join the setup network | Nothing (see [security.md](security.md#known-risks)) |
| Layout changed after the first release | Updates are refused for the other layout ("cannot run on this device") | The device itself |
