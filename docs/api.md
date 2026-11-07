# API

REST endpoints served by the device. All bodies are JSON unless noted.

> Outline only. This page is filled in before the v0.1.0 release.

## Status and health

`GET /api/health`, `GET /api/status`.

## Wi-Fi

`GET /api/scan`, `POST /api/connect`, `POST /api/disconnect`.

## Settings

`GET /api/settings`, `POST /api/settings`, `POST /api/reboot`, `POST /api/reset`.

## Updates

`POST /api/update`, `POST /api/update/check`, `POST /api/update/install`, `GET /api/update/status`, `POST /api/update/rollback`.

## Errors

Response format and error reasons.
