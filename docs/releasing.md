# Releasing

How versions are bumped, tagged and published.

> Outline only. This page is filled in before the v0.1.0 release.

## Versioning

`VERSION` file, `MAJOR.MINOR.PATCH`, changelog section.

## Cutting a release

PR bumping `VERSION` and `CHANGELOG.md`, merge, push tag `vX.Y.Z`.

## The pipeline

Checks workflow and release workflow, step by step.

## Scratch-repo pull test

Testing the real HTTPS, redirect and "latest" path before each release.

## Publishing

Pre-release first, check assets, then make it public.

## Bad releases

Mark as pre-release, ship a higher fixed version.

## GitHub setup

Protected Environment with `OTA_SIGNING_KEY`, `v*` tag ruleset, private vulnerability reporting.

## Manual hardware checklist

The checks run on a real ESP32 before every release.
