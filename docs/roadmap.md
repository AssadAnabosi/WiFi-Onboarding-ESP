# Roadmap

Planned work after v0.1.0, in no particular order. Nothing here is promised; open an issue to discuss an item.

| Item | Why |
| --- | --- |
| Force a new access point password on first setup | The default `12345678` is public |
| Password on the update endpoints | Today anyone on the network can start an update, roll back or downgrade to an older signed version (see [security.md](security.md#known-risks)) |
| More build targets, e.g. ESP32-S3, or 4 MB WROVER modules with PSRAM (`esp32-4mb-psram`; 8 MB ones have `esp32-8mb-psram`) | Each gets its own profile, partition table and manifest entry |
| Beta channel | Let test devices install pre-release versions |
| Optional silent auto-install | Install found updates without a click, for unattended devices |
| Manifest URL editable in Settings | Follow another repository or a self-hosted server without rebuilding |
| Minimum allowed version | Block downgrades to versions with known problems |
| Verify WPA2-Enterprise on a real network | It compiles and is wired up, but hasn't been tested |
