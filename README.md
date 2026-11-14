# DingoDiscordPresence

Discord Rich Presence for DingoEngine games, as a small C++20 static library for Windows and Linux.

It speaks the Discord client's local pipe protocol itself: no Discord SDK, no DLL beside the game, no account
linking. When Discord is not running, the game logs one line and carries on. No call waits on Discord.

Used by *Headstone* and *200 OK*.

## Use

```cpp
#include <DingoDiscordPresence/DiscordPresence.h>

Dingo::DiscordPresence::Config config;
config.AppId = "1234567890123456789";   // your Discord application's id
Dingo::DiscordPresence::Client client(config);

Dingo::DiscordPresence::Activity activity;
activity.Details = "Wave 12";
activity.State = "The Churchyard";
activity.LargeImage = "logo";           // an art asset uploaded for the application
client.SetActivity(activity);

client.Update();                        // once a frame
```

## Build

| | Windows | Linux |
|---|---|---|
| Generate | `scripts\windows\Generate.bat` | `scripts/linux/Generate.sh` |
| Build | `scripts\windows\Build.bat [Debug\|Release\|Distribution]` | `scripts/linux/Build.sh [config]` |
| Test | `scripts\windows\Test.bat [config]` | `scripts/linux/Test.sh [config]` |
| Package | `scripts\windows\Package.bat [config...]` | `scripts/linux/Package.sh [config...]` |

Windows needs Visual Studio with the C++ workload (toolset v145). Linux needs GCC 13 or newer and `make`.

`Package` writes one zip per configuration into `build/package/`: the headers, the static library, the fake
Discord pipe for captures, the licence and `VERSION.txt`. A release on GitHub carries the same six zips.

## Licence

MIT, see `LICENSE.txt`.
