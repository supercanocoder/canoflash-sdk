# Lobby

[All examples](../README.md) · [Entry point](src/main.cpp)

A complete room-selection interface: connect, create a public/private room,
join by six-digit code, find a game, show player names and start a small
moving-player demo. Built with Butano, with **no audio**.

## Build and run

Set the [shared example key](../README.md#set-your-game-key), then from the SDK root:

```sh
make -C examples/lobby LIBBUTANO=/path/to/butano/butano -j2
```

Output: `examples/lobby/lobby.gba`. You need devkitARM, Python 3 and a Butano
directory containing `butano.mak`; all graphics used by the example are bundled.

If rebuilding an older version that included audio, run `make clean` first
with the same `LIBBUTANO` setting. Both audio backends are set to `null`.

Load the ROM on each console and press A on the title screen to connect.
[Loading and hardware requirements](../../docs/getting-started.md#load-and-test).

## Try two players

1. Connect both consoles to open their sessions.
2. On the first, choose **Create a room**, a capacity and visibility.
3. On the second, choose **Join with a code** and enter the displayed digits.
4. Check that both names appear; the local player has `(you)`, the master `*`.
5. The master presses A to start when at least two players are present.
6. Move with the D-pad and check that the other console receives movement.
7. Press START, then choose **Leave the match** to return to room selection.

Alternatively, choose **Find a game** on both consoles for public two-player
matchmaking. Explicit room creation offers two to four seats. Room parameters
are `"demo"`.

## Controls

| Screen | Controls |
| --- | --- |
| Title | A connects |
| Menus | Up/down selects; A confirms; B returns |
| Code entry | Up/down changes digit; left/right moves; A joins; B cancels |
| Lobby | A starts for the master with at least two players; B leaves |
| Match | D-pad moves; START opens pause |
| Pause | Resume or leave; B resumes |

Leaving the room preserves the online session. B from room selection closes
the session and returns to the title.

## Structure

```text
title → session → room selection → lobby → match
                      ↑                    |
                      └──── leave ─────────┘
```

| File | Responsibility |
| --- | --- |
| [src/main.cpp](src/main.cpp) | Title, connection, session loop and menu keepalive |
| [src/scene_menu.cpp](src/scene_menu.cpp) | Room options, code input and join errors |
| [src/scene_lobby.cpp](src/scene_lobby.cpp) | Names, occupancy, master and start request |
| [src/scene_match.cpp](src/scene_match.cpp) | Position transport and pause menu |
| [src/ui.cpp](src/ui.cpp) | Text, navigation and frame handover |
| [include/lobby_name.h](include/lobby_name.h) | Font-safe display names |

## Keep the session alive

Menus call `on_frame` once per frame. In this example it polls without
consuming game messages:

```cpp
void keep_alive() {
    cf_poll(nullptr, 0);
    // The actual implementation also displays a reconnecting notice.
}
```

The match polls itself, because it needs received positions. During recovery,
the UI keeps advancing and the SDK keeps attempting transport recovery.
Do not copy a second menu poll into the same frame of a game that already polls.

This example sends no reliable events. If you add them, receive/process them
while menus are open; NULL-output polls retain them and queues are bounded.

## Presentation

The interface uses a white background, the CanoFlash multiboot logo, black
8x16 text and a black menu arrow. The font's white edge blends into the backdrop.
Connection-error messages hide the logo.

The logo is bundled in `graphics/logo.bmp`; no external ROM checkout is needed.
The white backdrop is set in code and does not require a background image.
The `selector` sprite represents players in the moving demo.

Names refresh as room metadata arrives. Supported Spanish accents are retained;
unsupported glyphs, malformed UTF-8 and control characters display as `?`.
Names are shortened to fit local/master/away markers. The SDK itself preserves
UTF-8; this conversion is presentation code.

## Data and limitations

Position messages contain x/y only. `cf_last_sender()` routes each received
position to the relay-assigned seat. Use matching ROM builds on all consoles.

The demo is not a complete game protocol: it does not exchange ready/version
messages, implement reliable actions or resynchronise an authoritative world
after master migration. Follow [Integration](../../docs/integration.md),
[Rooms](../../docs/rooms.md) and [Reliable events](../../docs/reliable.md)
when extending it.

Font attribution and licence: [Third-party notices](../../THIRD_PARTY.md).
