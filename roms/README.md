# Download example ROMs

[SDK overview](../README.md) · [Example sources](../examples/README.md)

These `.gba` files are ready to load: they include the public CanoFlash demo
game key, so you do not need to register your own game or compile them first.
Each player still needs a CanoFlash device configured for Wi-Fi, linked to
their own account and running firmware compatible with SDK 1.0.0.

| ROM | Download | Demonstrates |
| --- | --- | --- |
| Hello world | [hello_world.gba](https://raw.githubusercontent.com/supercanocoder/canoflash-sdk/main/roms/hello_world.gba) | Two-player movement, plain C |
| Hello world with Butano | [hello_world_butano.gba](https://raw.githubusercontent.com/supercanocoder/canoflash-sdk/main/roms/hello_world_butano.gba) | Two-player movement, Butano C++ |
| Lobby | [lobby.gba](https://raw.githubusercontent.com/supercanocoder/canoflash-sdk/main/roms/lobby.gba) | Rooms, player names, codes and reconnection; white interface, no audio |

## Run a demo

1. Download the same ROM for both consoles and load it with a compatible
   flashcart or loader. These are cartridge-format ROMs, not dedicated
   multiboot executables; see [Loading and testing](../docs/getting-started.md#load-and-test).
2. Connect each console to its configured CanoFlash. The device can detect the
   SDK from GBA or Multiboot mode, or you can select Online before connecting.
3. The two Hello world demos connect automatically. In Lobby, press **A** on
   the title screen, then create, join or find a room.
4. Test movement and room operations with the same example on each console.

The two Hello world variants share matchmaking parameters but use different
coordinate conventions: do not mix them in one match. The demo namespace is
shared by everyone using these downloads. For a controlled session with a
friend, use a private room and its code in Lobby.

These builds depend on the demo registration remaining enabled. For your own
game or independent testing, [register your own key](../docs/api-keys.md) and
rebuild the sources. Ordinary emulator Link support does not provide the
CanoFlash Internet connection.

## Build record

- SDK and example sources: [v1.0.0](https://github.com/supercanocoder/canoflash-sdk/tree/v1.0.0), commit `3780b60`.
- Built on 2026-09-13 with devkitARM 15.2.0 and Python 3.14.4.
- Butano: unmodified revision `92e94dd6f3b5539132a9f1b152991b7934d90739`.
- Game configuration: [demo_config.h](demo_config.h), containing a public game identifier, not device or account credentials.
- File integrity: [SHA256SUMS](SHA256SUMS).
- Dependency notices: [Third-party notices](../THIRD_PARTY.md#compiled-example-roms).

To reproduce the demos, copy `demo_config.h` to
`examples/canoflash_example_config.local.h` (back up any existing local
configuration first), then clean and rebuild all examples using the commands
in [Examples](../examples/README.md#build). Set `LIBBUTANO` to the checkout above.

Compilation and the embedded game key were checked for these binaries. A new
hardware run was not performed for this binary publication; see the existing
[validation scope](../docs/compatibility.md#validation-scope).
