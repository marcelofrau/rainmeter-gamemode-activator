# Game Mode Activator

![License](https://img.shields.io/badge/license-MIT-green)
![Version](https://img.shields.io/badge/version-1.0.0-blue)
![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Language](https://img.shields.io/badge/language-C-blue)
![GitHub release](https://img.shields.io/github/v/release/USER/rainmeter-gamemode-activator)

> Substitua `USER` pelo seu username do GitHub no badge acima para
> que ele aponte para o release correto.

Monitors Windows for running games and switches the tray icon between sleep/game mode.
When a game is detected, it spawns `gamemode_active.exe` as a marker process so other
tools (like **Rainmeter**) can detect game mode by checking the process list.

## How it works

Every 2 seconds (configurable) the foreground window is inspected using these heuristics:

- **Fullscreen**: window covers the entire monitor
- **Borderless**: window has no `WS_CAPTION` / `WS_THICKFRAME` (typical of games)
- **Graphics modules**: process loaded `d3d9.dll`, `d3d11.dll`, `opengl32.dll`, `vulkan-1.dll`, etc.
- **TopMost**: window with `WS_EX_TOPMOST` style

If any condition is met, a game is considered active.

## Marker process (`gamemode_active.exe`)

When game mode activates, a tiny marker process (`gamemode_active.exe`) is spawned.
It uses zero CPU (waits on an event that never signals). When game mode deactivates,
the marker is terminated.

This allows Rainmeter to detect game mode simply by checking if `gamemode_active.exe`
exists in the process list using `Plugin=Process`.

## Build

Requirements: **MinGW** (gcc) or any C11 compiler.

### PowerShell (recommended)

```powershell
.\build.ps1              # compila os 2 executáveis em dist/
.\build.ps1 -NoStrip     # build without -O2
.\build.ps1 -SkipClean   # keep existing files in dist/
```

### Makefile

```bash
make                     # generates both exes in dist/
make clean               # removes dist/
```

### Manual

```bash
# Marker process
gcc -O2 -std=c11 -mwindows -o dist/gamemode_active.exe src/gamemode_active.c

# Main process
gcc -Wall -Wextra -O2 -std=c11 -I. -DVERSION=\"1.0.0\" -o dist/gamemode.exe src/main.c src/detector.c src/logger.c resources.o -lgdi32 -lpsapi -mwindows
```

Version comes from `version.txt`.

## Usage

```bash
dist\rainmeter-gamemode-activator-1.0.0-win64.exe          # runs in tray, polls every 2s
dist\rainmeter-gamemode-activator-1.0.0-win64.exe --interval 1
dist\rainmeter-gamemode-activator-1.0.0-win64.exe --logfile games.log
dist\rainmeter-gamemode-activator-1.0.0-win64.exe --once   # single check via message box
dist\rainmeter-gamemode-activator-1.0.0-win64.exe --version
dist\rainmeter-gamemode-activator-1.0.0-win64.exe --help
```

Right-click the tray icon for **About** and **Quit**.

### Log example

```
[2026-05-24 12:30:01] === Game Mode Activator started ===
[2026-05-24 12:30:01] >> Spawned marker process (PID: 12345)
[2026-05-24 12:30:01] >> GAME STARTED: eldenring.exe (PID: 12344, Title: "ELDEN RING", Flags: fullscreen borderless dx)
[2026-05-24 12:30:45] << GAME STOPPED: eldenring.exe (PID: 12344)
[2026-05-24 12:30:45] << Terminated marker process
```

## Rainmeter integration

Use `Plugin=Process` to check if `gamemode_active.exe` is running:

```ini
[MeasureGameMode]
Measure=Plugin
Plugin=Process
ProcessName=gamemode_active.exe
UpdateDivider=2

[GameModeActive]
Measure=Calc
Formula=(MeasureGameMode > 0 ? 1 : 0)
Substitute="1":"#GREEN$","0":"#GRAY$"
```

Or use `RunCommand` with `--once` for one-shot checks.

## License

MIT

## Icons

Icons by [Icons8](https://icons8.com/).
