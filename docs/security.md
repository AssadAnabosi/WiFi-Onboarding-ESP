# Security

How updates are protected and what the known risks are.

> Outline only. This page is filled in before the v0.1.0 release.

## Signed updates

ECDSA P-256 signatures checked by the ESP32 core; the info block; core version requirements.

## Trusted keys

Release and dev keys, where they live, key rotation and retiring a leaked key.

## Cross-site request protection

Content types, `Origin` and `Host` checks on every POST route.

## Secure Boot

Not used by this project; recommended for production devices.

## Known risks

Unencrypted settings in flash, no update password, the public default AP password, forks that keep the upstream key.

## Reporting a vulnerability

See [SECURITY.md](../SECURITY.md).
