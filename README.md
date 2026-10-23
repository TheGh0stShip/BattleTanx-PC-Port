# BattleTanx PC Port

[![Build](https://github.com/TheGh0stShip/BattleTanx-PC-Port/actions/workflows/build.yml/badge.svg)](https://github.com/TheGh0stShip/BattleTanx-PC-Port/actions/workflows/build.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](COPYING)

A native source port of **BattleTanx** for Linux and Windows, built from the
matching decompilation of the Nintendo 64 USA Rev 0 release.

The port is under active development. It currently provides native rendering,
audio, input, saving, and a playable campaign battle, but it is not yet a
complete replacement for the original game. See [Project status](#project-status)
for the current scope.

This project does not include game data. You must supply your own legally
obtained copy of the supported retail ROM.

## Features

- Native Linux and Windows executables
- Keyboard and hot-pluggable SDL game-controller support
- Controller rumble and up to four assigned controller ports
- Native software rendering of decoded retail worlds, models, textures, and HUD
- Native sound-effect and music playback
- Original 30 Hz gameplay timing and recovered tank, weapon, collision, enemy,
  pickup, and mission behavior
- Original Controller Pak save format in a standard 32 KiB `.mpk` image
- Windowed and borderless-fullscreen modes
- Portable installations with local settings and saves
- ROM-free release packages

This is a source port, not an emulator. The executable contains native C++
implementations of recovered game behavior and host platform services. The ROM
is opened read-only as a source of retail game data.

## Quick start

Download a package from a successful
[workflow run](https://github.com/TheGh0stShip/BattleTanx-PC-Port/actions/workflows/build.yml),
extract it, and launch the executable with the path to your ROM:

### Linux

```sh
./BattleTanx /absolute/path/to/BattleTanx.z64
```

### Windows

```powershell
.\BattleTanx.exe C:\Games\BattleTanx.z64
```

The verified ROM path is remembered after the first successful launch. To
check a ROM without starting the game, use:

```sh
BattleTanx --validate /path/to/BattleTanx.z64
```

Maintainers can run a bounded native gameplay check with `--smoke`. It creates
the campaign runtime, advances through the title and countdown, drives and
fires the player vehicle, renders 240 frames, verifies the resulting state,
prints a deterministic checksum, and exits. It still requires the supported
retail ROM and writes only to the configured user-data directory.

## Supported ROM

| Property | Required value |
|---|---|
| Game | BattleTanx |
| Region and revision | USA, Rev 0 (`NBXE`) |
| Format | Big-endian `.z64` |
| Size | 8 MiB |
| SHA-256 | `c5b7cf3523de025e3f18e2c8df2deb0eced5613e7c46cb8c6c5a4f22644ead3f` |

Other regions, revisions, byte orders, and modified ROMs are not supported.
ROMs and extracted game assets must not be added to this repository or included
in distribution packages.

## Controls

Press **Start** or **Enter** to begin. The default retail control preset keeps
the turret aligned with the hull.

| Action | Keyboard | Controller |
|---|---|---|
| Drive and steer | **W A S D** | Left stick |
| Fire cannon / N64 Z | **Left Shift** | Either trigger |
| N64 A | **X** | South face button |
| N64 B | **Z** | East or west face button |
| Start / pause | **Enter** | Start |
| N64 L / R | **Q / E** | Left / right shoulder |
| D-pad | Arrow keys | D-pad |
| C buttons | **I J K L** | Right stick |
| Toggle fullscreen | **F11** or **Alt+Enter** | — |
| Quit | **Escape** | — |

Controllers can be connected or removed while the game is running. They are
assigned to ports in connection order. Keyboard input remains available for
port one.

## Saves and settings

BattleTanx saves to a Controller Pak. The port stores the original game record
inside `battletanx-pak1.mpk`, a standard 32 KiB Controller Pak filesystem image
compatible with common N64 save tools. The original record layout and checksum
are retained.

PC-specific options are stored separately in `settings.ini` and include:

- Window mode and dimensions
- Master, music, and effects volume
- Controller deadzone, sensitivity, and rumble strength
- The last verified ROM path

By default, writable files use the operating system's per-user data directory.
Place an empty `portable.txt` beside the executable to keep them in a neighboring
`userdata` directory instead. `BATTLETANX_DATA_DIR` can override the location
for scripted or shared installations.

## Building from source

Requirements:

- CMake 3.20 or newer
- A C++20 compiler
- Git, used by CMake to obtain the pinned SDL2 dependency
- Ninja or another CMake-supported build tool

### Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/BattleTanx /absolute/path/to/BattleTanx.z64
```

### Windows

Open a Developer PowerShell for Visual Studio 2022, then run:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
.\build\BattleTanx.exe C:\Games\BattleTanx.z64
```

The build downloads SDL2 source at configure time and links it statically. No
retail game data is generated, copied, or embedded during the build.

## Installing and packaging

Install a local ROM-free build:

```sh
cmake --install build --prefix ./BattleTanx-PC
```

For a multi-configuration Visual Studio build, add `--config Release`.

Create the same archive format used by continuous integration:

```sh
cmake --build build --target package --parallel
```

Linux packages are `.tar.gz` archives and Windows packages are `.zip` archives.
Each package contains the executable, README, and license. A SHA-256 sidecar is
generated beside the archive. Neither contains a ROM or extracted retail assets.

Configure with `-DBATTLETANX_PORTABLE=ON` to include `portable.txt` and add a
`-portable` suffix to the package name.

## Project status

The current build launches the campaign wave selected by the original save,
loads its retail world and population data, and runs native battles with all
three vehicle rosters, enemies, weapons, pickups, turrets, queen objectives,
HUD, audio, mission results, respawning, Controller Pak progress updates, wave
retry, retail mission briefing art and objective/result messages, and
in-process progression to the next mission. The briefing screen supports
controller or keyboard D-pad lineup selection and saves it to the Controller Pak.

Work still in progress includes:

- Remaining front-end menus and chapter intermissions
- Remaining objective presentation and uncommon queen/base interactions
- Remaining scripted enemy behaviors, weapons, effects, and pickup cases
- Complete music selection and uncommon material/rendering modes
- Broader playthrough, controller, platform, and long-session validation

Missing or approximate behavior is tracked as development work; it is not
presented as retail-complete. Architecture and portability details are kept in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Contributing

Bug reports and focused pull requests are welcome. Please keep changes within
the native source-port architecture, include tests for portable core behavior,
and verify both the Linux and Windows build paths where practical.

Do not submit ROMs, extracted assets, generated assembly, binaries, crash dumps,
or other copyrighted retail data. The matching decompilation and this native
port have separate responsibilities; portability changes belong here.

## License

Project source is licensed under GPL-3.0-or-later. See [COPYING](COPYING).

BattleTanx and all original game data remain the property of their respective
owners. This project is not affiliated with or endorsed by the game's original
publisher or developers.
