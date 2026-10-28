# Classic multiplayer

`dbc` only; DBPro multiplayer commands stay unsupported. DirectPlay no longer
exists, so OpenDBC games talk to other OpenDBC games over
[ENet](https://github.com/lsalzman/enet) (UDP with reliable ordered delivery).
They cannot reach a game running on the original DarkBASIC.

## Connection and sessions

`PERFORM CHECKLIST FOR NET CONNECTIONS` lists one connection, numbered 1, under
DirectPlay's TCP/IP name. `SET NET CONNECTION 1, address$` accepts:

| Address | Sessions are looked for | A created game listens on |
| --- | --- | --- |
| empty | on the local network (broadcast) and on this machine | the first free port from 24600 to 24603 |
| `host` | at that name or IPv4 address | the first free port from 24600 to 24603 |
| `host:port` or `:port` | at that port only | that port |

Without `SET NET CONNECTION`, the commands use connection 1 with an empty
address. `PERFORM CHECKLIST FOR NET SESSIONS` waits half a second for answers and
lists each running game's name. `JOIN NET GAME` uses that list. If no list was
taken since the connection was set, it takes one first, so `JOIN NET GAME 1, name$`
works on its own as in the Classic examples.

A game that cannot be created or joined is not an error. This happens when the
port is taken, the session is missing or full, or the host does not answer within
5 seconds. `NET GAME EXISTS()` stays 0 and a warning is logged. Invalid arguments
are errors, and so is a second game before `FREE NET GAME`.

## Players

The host numbers players from 1 and reuses the lowest free number, so the
numbers stay within the game's maximum. A player has the same number on every
machine. In the players checklist:

| Field | Value |
| --- | --- |
| A, B | the player number |
| C | 1 for a player this application created |
| D | 1 for the host's own player |

`CREATE NET PLAYER` adds a local player and returns 0 when the game is full.
`FREE NET PLAYER` only removes players that `CREATE NET PLAYER` added.
`NET PLAYER CREATED()` and `NET PLAYER DESTROYED()` return, one per call, the
players that other applications added or removed.

## Messages

Every message travels through the host, so both `CREATE NET GAME` flags (1 peer
to peer, 2 client/server; Classic examples pass 0, read as 1) behave alike.
Integer, float and string messages are reliable and arrive in order. Player 0
sends a copy to every other player. Each copy names its recipient in
`NET MESSAGE PLAYER TO()`, including copies for several local players of one
application. A message to a player who has left is dropped.

`GET NET MESSAGE` makes the oldest received message current, or clears the
current message when none is waiting. `NET MESSAGE EXISTS()` is 1 while a current
or waiting message exists, so both Classic message loops end:
`get; while exists: …; get` and `repeat: …; get: until exists() = 0`.
Without a current message, the readers return 0 or an empty string. Reading
another type than `NET MESSAGE TYPE()` fails.

When the host leaves, the others get `NET GAME LOST() = 1` and `NET GAME EXISTS() = 0`.
There is no host migration yet, so `NET GAME NOW HOSTING()` is always 0.

## Memblocks and media

| Type | Sent with | Received with |
| --- | --- | --- |
| 4 | `SEND NET MESSAGE MEMBLOCK player, memblock[, guarantee]` | `NET MESSAGE MEMBLOCK memblock` |
| 5 | `SEND NET MESSAGE IMAGE player, image, guarantee` | `NET MESSAGE IMAGE image` |
| 6 | `SEND NET MESSAGE BITMAP player, bitmap, guarantee` | `NET MESSAGE BITMAP bitmap` |
| 7 | `SEND NET MESSAGE SOUND player, sound, guarantee` | `NET MESSAGE SOUND sound` |
| 8 | `SEND NET MESSAGE MESH player, mesh, guarantee` | `NET MESSAGE MESH mesh` |

Each one travels in the layout of the matching `MAKE MEMBLOCK FROM` command, at
most 16 MB. The receiving command replaces any memblock, image, bitmap, sound or
mesh with that number, as `MAKE ... FROM MEMBLOCK` does. These commands need a
current message of their own type.

A guarantee of 0 (the default of the two-argument memblock form) sends the
message on a separate unreliable channel. It may arrive out of order with other
messages, or be dropped whole when one of its fragments is lost. Any other value
makes it reliable and ordered like the other messages.

## Implementation

`NetworkSystem` (`src/engine/systems/network_system.cpp`) runs ENet on its own
thread, from `CREATE NET GAME` or `JOIN NET GAME` until `FREE NET GAME` or the end
of the program. The host therefore relays messages even while the program is busy.
Discovery queries reach the game port and are answered before ENet sees them.
Malformed packets disconnect the peer that sent them.
