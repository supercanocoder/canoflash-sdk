# Contributing

Thanks for helping improve CanoFlash Net. This checkout contains the GBA SDK,
examples and developer documentation. Device firmware and relay deployment
are maintained separately from the standalone SDK.

## Report a problem

Use the [issue tracker](https://github.com/supercanocoder/canoflash-sdk/issues). Include the SDK revision,
firmware version, console/loader, toolchain, number of players and exact
reproduction steps. Start from a bundled example where possible.

Describe expected and observed behaviour. Include status/counters and sanitised
logs, but no account passwords, device credentials or private network details.

## Build before submitting

Follow [Getting started](docs/getting-started.md), then build all examples:

```sh
make -C examples/hello_world
make -C examples/hello_world_butano LIBBUTANO="$LIBBUTANO" -j2
make -C examples/lobby LIBBUTANO="$LIBBUTANO" -j2
git diff --check
```

Compilation does not need hardware or a registered key. Testing online does.
Keep personal example configuration in the ignored local header described in
[Examples](examples/README.md#set-your-game-key).

For code changes, describe which hardware scenarios you tested and what remains
untested. Use the [hardware validation checklist](docs/compatibility.md#hardware-validation).
A successful emulator run does not validate the physical Link connection.

## Scope changes clearly

- Keep the core usable from C99 without an engine dependency.
- Preserve C++ linkage and document all public API changes.
- Update examples and compatibility notes with changed contracts.
- Treat sender identity, queue limits and uncertain delivery outcomes explicitly.
- Coordinate changes to duplicated SPI constants with firmware and relay.
- Validate timing changes on hardware; do not infer safe timings from host tests.

Documentation should distinguish compilable examples from integration fragments,
defaults from hard guarantees, and game responsibilities from transport behaviour.

## Prepare a standalone SDK release

Build from a clean checkout of this repository, without relying on sibling
project folders. Preserve licence notices. Do not include ignored build
outputs or the local key header in source archives.

Published example binaries belong in `roms/`, where they are intentionally
tracked. When updating them, record their source revision, toolchain and online
configuration in `roms/README.md`, and refresh `roms/SHA256SUMS`.

The first public release is SDK 1.0.0 with CanoFlash firmware 1.0.0. Before
creating its release tag, record the exact SDK revision, toolchain and hardware
validation results. For subsequent releases, update the header version,
changelog and firmware compatibility table together. Publish measured
limitations alongside each release.
