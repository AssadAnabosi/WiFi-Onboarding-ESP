# OTA updates

The device updates itself over Wi-Fi, by a file you upload or by a release it finds on GitHub, and never loses its settings doing so. Both paths share one set of checks, and only one update runs at a time.

Every update is a signed `.ota.bin` file: firmware and web interface together, for one build target. Unsigned files, files signed by an unknown key and files for another chip are always refused. See [security.md](security.md) for how signing works.

## Manual upload

1. Open **Settings → Firmware** and pick a `wifi-onboarding-<version>-<target>.ota.bin` file.
2. The browser reads the file's info block and shows its version and target, plus a warning when one applies (see [Compatibility rules](#compatibility-rules)). Files that can never install are blocked here, before anything is sent.
3. Click **Install**. The file is sent to the device, which writes it into the unused firmware slot while the progress bar fills.
4. The device checks the file's info block and signature, switches to the new slot and reboots. The page waits for the device to come back and reloads.

If the browser is closed or the connection drops mid-upload, the device cancels the upload after 30 s without data, and the old firmware keeps running.

## Update checks

The device looks for a new release on GitHub on its own:

- 30 s after boot,
- then every 24 h, plus a random delay of up to 1 h so devices don't all check at once,
- and whenever you click **Check for updates** in Settings → Firmware.

A check reads the release manifest (`manifest.json`) of the latest release of the GitHub repository set in `ProjectConfig.h`. When the latest release has a newer version for this device's build target:

- the Home page shows an **"Update vX.Y.Z available"** badge, linking to Settings → Firmware;
- the Firmware card shows the version and its release notes (as plain text);
- nothing installs until you click **Install**. The device then downloads the file in the background, checks its size and SHA-256 against the manifest, and runs the same checks as a manual upload.

Requirements for a check or download:

- The device must be connected to a Wi-Fi network with internet. In access-point-only mode it reports "No internet".
- At least 60 KB of free memory, for the secure (HTTPS) connection.

A check never offers an older version or the version already installed. Pre-releases are never offered.

## Compatibility rules

The browser applies these rules before sending a file, and the device applies them again before installing.

| Case | Manual upload | Update check |
| --- | --- | --- |
| Newer version, same build target | Installs | Offered |
| Same or older version | Installs after a confirmation | Never offered |
| Same chip and flash layout, other configuration (e.g. `esp32-4mb-relay` on an `esp32-4mb` device) | Installs after a warning | Never offered |
| Other chip, other flash layout or other project | Blocked | Never offered |
| Factory image (`.factory.bin`) | Blocked | Never offered |
| Unsigned, or signed by a key this firmware doesn't trust | Blocked | Rejected |

After installing another configuration's file, the device keeps warning on the Home page and the Firmware card until a file for its original configuration (its *home target*) is installed. Its home target only changes with a USB flash.

## Messages

Warnings you can confirm:

| Message | Meaning |
| --- | --- |
| "This file is vX. Installed: vY. This is a downgrade, continue?" | Installing an older version. Allowed by manual upload only |
| "This file is vX, the version already installed. Install it again?" | Reinstalling the same version |
| "This file is for `<target>`. This device runs `<target>`. … If you continue, future updates will follow `<target>`." | Another configuration of the same chip; future update checks follow the new one |
| "This restores the device's original configuration (`<target>`)." | The file matches the home target again; the mismatch warning clears |

Blocks:

| Message | Meaning |
| --- | --- |
| "This is a factory image for USB flashing, not an update. Use the file ending in `-<target>.ota.bin`." | You picked the `.factory.bin`. Use the `.ota.bin` from the same release |
| "This file is for `<target>` and cannot run on this device …" | Built for another chip or flash layout |
| "This file is for another project (`<slug>`)." | Signed update for a different project |
| "This file isn't signed for this project." | No signature, or a signature from a key this firmware doesn't trust |
| "This file is not a firmware update." | Not an update file at all |
| "The file is too large for this device" | Doesn't fit the firmware slot |

Update check results:

| Message | Meaning |
| --- | --- |
| "No internet: the device is not connected to a Wi-Fi network (access point only)" | Connect the device to a network first |
| "Not enough free memory for a secure download" | Free memory is below 60 KB; a reboot usually helps |
| "Could not reach the update server" / "The update server answered NNN" | Network problem, or the repository has no published release yet |
| "No update for this device (`<target>`)" | The latest release has no file for this build target |
| "This update needs a manual install" | The release uses a newer manifest format than this firmware understands. Upload its `.ota.bin` by hand |
| "The download doesn't match the release manifest" | Size or SHA-256 mismatch; nothing was installed |

## While an update finishes

New firmware starts *unconfirmed*. It confirms itself once setup has finished and it has run for 60 s more; the device doesn't need to reach your Wi-Fi network for that. Until then:

- every page shows a **"Finishing update, N s"** banner;
- reboot, settings save, factory reset, new updates and rollback are refused with "Finishing update, please wait N s";
- Wi-Fi scan and connect keep working.

**A reset or power cut during this window rolls the update back** to the previous version. No software can prevent that: it is how the bootloader protects against firmware that doesn't start. Wait for the banner to go away before unplugging the device.

## Rollback

Automatic rollback happens when new firmware:

- crashes or resets before it confirms itself: the bootloader starts the previous version on the next boot;
- hangs: the watchdog resets it, which rolls back the same way;
- runs but doesn't become healthy within 3 minutes of boot: it rolls itself back and reboots.

Manual rollback: after a successful update, Settings → Firmware shows **"Previous version: vX.Y.Z"** with a **Roll back** button. It returns to the version from right before the last update (0.2.0 → 0.3.0 → 0.4.0, then Roll back, gives 0.3.0), keeping settings. The older version then goes through the same confirmation window. The button is hidden after any rollback, a failed or incomplete update, or a USB flash.

## Failure behaviour

| Failure | Result |
| --- | --- |
| Upload or download interrupted, wrong checksum or bad signature | Nothing installed, no reboot; the current firmware keeps running |
| New firmware crashes or resets before confirming | The previous version boots |
| New firmware never becomes healthy | Rolled back at the 3-minute deadline |
| Power cut during the confirmation window | The previous version boots |

Settings (Wi-Fi credentials, access point settings, hostname) survive every update and rollback. They are only lost by a factory reset, flashing a `.factory.bin`, or erasing the flash over USB.
