# Examples

[SDK overview](../README.md) · [Setup guide](../docs/getting-started.md)

| Project | Engine | Output | Best for |
| --- | --- | --- | --- |
| [hello_world](hello_world/README.md) | Plain C / devkitARM | `hello_world.gba` | Learning the session and game loop |
| [hello_world_butano](hello_world_butano/README.md) | Butano | `hello_world_butano.gba` | Integrating from C++ |
| [lobby](lobby/README.md) | Butano | `lobby.gba` | Room menus, codes, names and reconnection UI |

The SDK core is shared by all three. You can compile with no device and no
registered key; online play needs a configured CanoFlash per console.

## Set your game key

All examples include [canoflash_example_config.h](canoflash_example_config.h).
Its default value is a placeholder, not a registered game key.

Create `examples/canoflash_example_config.local.h` with:

```c
#ifndef CANOFLASH_EXAMPLE_CONFIG_LOCAL_H
#define CANOFLASH_EXAMPLE_CONFIG_LOCAL_H

#define CF_EXAMPLE_API_KEY "cfn_REPLACE_WITH_YOUR_GAME_KEY"

#endif
```

Replace the value with your key from the [CanoFlash dashboard](https://canoflash.com).
This local file is ignored by Git. It lets you rebuild every example without
editing their source or publishing a shared test configuration.

If you do not want a local override, edit the default in
`canoflash_example_config.h` directly. The key is a public game identifier,
not an account credential.

After changing configuration, clean and rebuild. Use the same key on both
consoles. Test one example at a time: the small C and Butano demos use empty
matchmaking parameters, but their coordinate conventions differ, so they
should not be mixed in one game. The lobby uses `"demo"`.

## Build

Run from the SDK root:

```sh
make -C examples/hello_world
make -C examples/hello_world_butano LIBBUTANO=/path/to/butano/butano -j2
make -C examples/lobby LIBBUTANO=/path/to/butano/butano -j2
```

`LIBBUTANO` is the directory containing `butano.mak`, regardless of your
checkout's folder name. See [Getting started](../docs/getting-started.md) for
toolchain setup and loading.

## What the examples demonstrate

The moving-square demos use normal messages only. The lobby demonstrates room
and session management, not every SDK feature. Reliable-event submission and
application acknowledgements are illustrated in
[Reliable events](../docs/reliable.md).

The examples use small fixed GBA structs for positions. Public games should
define and validate an application protocol as described in
[Integration](../docs/integration.md#design-your-payloads). They do not implement
a complete authoritative game simulation or recovery snapshot protocol.
