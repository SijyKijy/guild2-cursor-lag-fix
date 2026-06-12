# The Guild 2: Renaissance cursor lag fix

[![build](https://github.com/SijyKijy/guild2-cursor-lag-fix/actions/workflows/build.yml/badge.svg)](https://github.com/SijyKijy/guild2-cursor-lag-fix/actions/workflows/build.yml)

Mouse-cursor lag fix for **The Guild 2: Renaissance** (Steam, appid 39680). Moving the mouse
tanked the FPS because the game redraws its cursor on every `WM_SETCURSOR`; this proxy
`dinput8.dll` throttles that message. The lag is gone and the game's own cursor is kept.

## How to use

1. Download `dinput8.dll` from the [latest release](https://github.com/SijyKijy/guild2-cursor-lag-fix/releases/latest)
   (or build it yourself: run `build.bat` — needs Visual Studio; builds x86 → `build\dinput8.dll`).
2. Copy `dinput8.dll` next to `GuildII.exe` in the game folder.
3. Launch the game.

To remove: delete `dinput8.dll` from the game folder.
