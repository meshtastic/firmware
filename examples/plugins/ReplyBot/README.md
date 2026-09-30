# ReplyBot (example plugin)

Example Meshtastic **module plugin** under `examples/plugins/`. It listens for slash commands on text messages (DM or primary-channel broadcast) and replies with a direct message that includes hop count, RSSI, and SNR.

## Commands

| Command  | Behavior              |
| -------- | --------------------- |
| `/ping`  | Mic-check style reply |
| `/hello` | Same                  |
| `/test`  | Same                  |

Reply text looks like: `Mic Check : N Hops away | RSSI … | SNR …`

Rate limits: 15 s per sender on DMs, 60 s on primary-channel broadcasts.

## Installation

Add the PlatformIO dependency to your desired target, or create a derived env that pulls in this library (example for T-Deck):

```ini
[env:t_deck_replybot]
extends = env:t-deck
lib_deps =
  ${env:t-deck.lib_deps}
  symlink://examples/plugins/ReplyBot
```

Build and flash:

```bash
pio run -e t_deck_replybot
pio run -e t_deck_replybot -t upload
```
