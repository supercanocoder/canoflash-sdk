# Getting started

[Documentation](README.md) · Next: [Integration](integration.md)

From a fresh SDK checkout to two consoles exchanging positions.

To try the demos without a toolchain, download the
[compiled example ROMs](../roms/README.md). They include a shared demo game key;
the steps below are for building the examples or developing your own game.

## Get the SDK

Clone the public repository and select the release used by your game:

```sh
git clone https://github.com/supercanocoder/canoflash-sdk.git
cd canoflash-sdk
git checkout v1.0.0
```

Alternatively, download and extract the
[v1.0.0 source archive](https://github.com/supercanocoder/canoflash-sdk/archive/refs/tags/v1.0.0.zip).
The SDK, examples and documentation are included; Butano is installed separately.

## Install the toolchain

Install devkitPro's GBA development tools, including devkitARM and `gbafix`.
Follow the [devkitPro installation instructions](https://devkitpro.org/wiki/Getting_Started)
for your operating system. You also need `make` in the build shell.

A typical macOS/Linux installation uses:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
"$DEVKITARM/bin/arm-none-eabi-gcc" --version
```

Use your actual paths. On Windows, use the environment provided by your
devkitPro installation rather than copying Unix paths.

For Butano examples, also install Python 3 and Butano following
[Butano's setup guide](https://gvaliente.github.io/butano/getting_started.html).
`LIBBUTANO` must point to the directory containing `butano.mak`:

```sh
export LIBBUTANO=/path/to/butano/butano
test -f "$LIBBUTANO/butano.mak"
python3 --version
```

All following commands run from the root of this SDK checkout. No sibling
ESP32, web backend or ROM project is needed to build the examples.

## Configure the device and game

1. Configure Wi-Fi on each CanoFlash and link it to its owner's account.
2. Install firmware compatible with this SDK. See [Compatibility](compatibility.md).
3. Register a game in the online-games section of your [CanoFlash dashboard](https://canoflash.com).
4. Put its key in the [local example configuration](../examples/README.md#set-your-game-key).

The game key identifies the game; the device authenticates the player.
Do not copy account passwords or device tokens into a ROM.

## Build

Plain C:

```sh
make -C examples/hello_world
```

Output: `examples/hello_world/hello_world.gba`.

Butano:

```sh
make -C examples/hello_world_butano LIBBUTANO="$LIBBUTANO" -j2
```

Output: `examples/hello_world_butano/hello_world_butano.gba`.

Lobby interface:

```sh
make -C examples/lobby LIBBUTANO="$LIBBUTANO" -j2
```

Output: `examples/lobby/lobby.gba`. Press A on the title screen to connect.
The two smaller examples connect automatically.

After changing the key, clean and rebuild your chosen example:

```sh
make -C examples/hello_world clean
make -C examples/hello_world
```

For Butano, supply the same `LIBBUTANO` setting to clean and build.

## Load and test

Load the same example on both consoles, each with its own CanoFlash.
The supplied Makefiles build cartridge-format ROMs for a compatible flashcart
or loader. They do not produce a dedicated multiboot executable.

Loading a ROM through CanoFlash and compiling a multiboot-format program are
different operations. The lobby has also been exercised through the project's
CanoFlash loading workflow. Whether another ROM can be loaded that way depends
on its size, memory layout and the loader; the SDK itself does not upload ROMs.

With the plain C example:

1. The connecting indicator runs while the session opens.
2. The first console waits in a public two-seat room.
3. The second joins if the key, parameters and capacity match.
4. The master requests the playing state; both consoles then show movement.
5. Move with the D-pad and check the other console receives your position.

One console can verify connection and room creation. A second is needed for
data exchange. An emulator helps with rendering and ROM debugging, but does
not replace CanoFlash hardware for end-to-end network tests.

## Move into your own game

Copy the two SDK files or vendor this folder, then follow
[Integration](integration.md). Record the SDK revision and device firmware used.

Before sharing a ROM, test missing hardware, failed connection, leaving a room,
and Wi-Fi recovery as well as successful play. See
[hardware validation](compatibility.md#hardware-validation).
