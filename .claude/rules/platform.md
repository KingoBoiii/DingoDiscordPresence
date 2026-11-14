---
paths: ["src/Platform/**"]
---

This is the fragile part of the library. A mistake here stalls a game's frame or kills the process.
Implementation and review run at high effort.

- **Nothing blocks.** The only bounded waits are `Pipe::Flush` (the caller's milliseconds) and the
  50 ms guard on a read that `PeekNamedPipe` already said is there.
- **Windows** (`WinPipe.cpp`, the only file with `<Windows.h>`):
  - Open with `CreateFileW(..., FILE_FLAG_OVERLAPPED, ...)`, one try per name. Never `WaitNamedPipe`.
  - One overlapped write in flight at a time. Its buffer and its `OVERLAPPED` are members and must
    outlive the write: before closing the handle, `CancelIoEx` and wait for the cancel to finish.
  - While a write is in flight, the next one is `Dropped`, not queued behind a blocking call.
  - Read only what `PeekNamedPipe` reports.
  - Close every handle, the two events included.
- **Linux** (`PosixPipe.cpp`, the only file with `sys/socket.h` and `unistd.h`):
  - `SOCK_NONBLOCK | SOCK_CLOEXEC` on the socket.
  - Every `send` is `MSG_NOSIGNAL | MSG_DONTWAIT`: a closed peer otherwise raises `SIGPIPE` and kills
    the game.
  - A short send breaks the framing: it is `Failed`, never retried from the middle.
  - A path too long for `sun_path` is skipped, never truncated.
  - The socket is `discord-ipc-N` under the first of `$XDG_RUNTIME_DIR`, `$TMPDIR`, `$TMP`, `$TEMP`,
    else `/tmp`, and under the Flatpak (`app/com.discordapp.Discord/`) and Snap (`snap.discord/`)
    subfolders there.
- Both files implement the one `Pipe` class in `Pipe.h`. A behaviour change goes into both, with a test
  that runs on both.
