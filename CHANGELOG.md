# Changelog

## Unreleased

- Added downloadable ROMs for all three SDK 1.0.0 examples, built with a
  registered public demo game key, build notes, checksums and runtime notices.
- The `v1.0.0` tag remains the original source release; compiled downloads are
  maintained in `roms/` on `main`.

## 1.0.0 — 2026-09-13

First public release: tag `v1.0.0` in
[supercanocoder/canoflash-sdk](https://github.com/supercanocoder/canoflash-sdk).

Compatible with **CanoFlash firmware 1.0.0**.

- C99 SDK with C++ linkage, no engine dependency and no dynamic allocation.
- Separate sessions and rooms, public matchmaking and private room codes.
- Player names, seat metadata, room states and master migration.
- Normal state messages and reliable events, up to 480 bytes each.
- Sender slot/generation metadata and recovery of reserved seats.
- Three buildable examples, including the lobby with a white interface and no audio.
- Installation, integration, API, compatibility and troubleshooting guides.

See [Compatibility](docs/compatibility.md) for protocol requirements and
validation scope, and [Reliable events](docs/reliable.md) for delivery limits.

The 0.1.0 identifier used during development was not a public release.
