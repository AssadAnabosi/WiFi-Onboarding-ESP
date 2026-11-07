# Architecture

How the firmware is put together.

> Outline only. This page is filled in before the v0.1.0 release.

## Flash layout

The `esp32-4mb` partition table and why it is frozen.

## Settings storage

NVS namespaces `cfg` and `sys`, setting names, defaults, factory reset.

## Build targets

Target naming, profiles, partitions files, target headers, `build_opt.h`.

## Update states

The shared update state machine and lock.

## Rollback

Bootloader rollback, the health check, slot records, USB-flash detection, home target.

## Generated files

`WebAssets.h`, `Version.h`, `OtaPublicKey.h` and how they are produced.
