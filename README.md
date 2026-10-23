# BattleTanx PC Port

This project is a native Linux and Windows source port of **BattleTanx** for
Nintendo 64 (USA Rev 0). It is under active development and is not yet a
playable game.

The port consumes game data from a legally obtained retail ROM supplied by the
user. ROMs, extracted assets, generated files, and retail game data must never
be committed or included in release packages.

## Supported ROM

- Format: big-endian `.z64`
- Region/revision: USA, Rev 0 (`NBXE`)
- Size: 8 MiB
- SHA-256: `c5b7cf3523de025e3f18e2c8df2deb0eced5613e7c46cb8c6c5a4f22644ead3f`

## Linux build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/BattleTanx /absolute/path/to/BattleTanx.z64
./build/BattleTanx --window /absolute/path/to/BattleTanx.z64
```

## Windows build

Open a Developer PowerShell for Visual Studio 2022, then run:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/BattleTanx.exe C:\Games\BattleTanx.z64
./build/BattleTanx.exe --window C:\Games\BattleTanx.z64
```

The current executable proves the portable build and ROM-loading boundary. It
validates the complete ROM, decompresses all 21 audited retail resource
bundles, and validates their offset tables before exposing entries to the
native runtime. Each original loader's destination capacity remains enforced.
It also decodes and validates all 22 recovered world streams and all 18
navigation streams, including group-to-placement ranges, shape references,
navigation vertex references, and triangle adjacency bounds.
Recovered navigation operations provide triangle centroids, edge midpoints,
strict point containment, blocked-kind filtering, and the original octagonal
nearest-cell search used during route initialization. Native straight-line
route traversal follows the recovered corner-adjacency rules and reports
target, boundary, blocked-cell, and retail 248-step limit outcomes.
The portable core also contains deterministic recovered controller-state and
planar-motion behavior covered by host tests. The default controller binding
preset is decoded from the verified retail ROM and evaluated with the recovered
analog/digital axis rules. The first native gameplay slice
adds a player-tank state, navigation-constrained movement, and a single
bounded projectile. The current M1A1 drive slice uses its recovered retail
turn, acceleration, grip, drag, and rotated footprint. Static collision uses
the recovered level-object type filter and each object's rotated level shape.
The temporary top-down
presentation remains bring-up code rather than a claim of complete retail
gameplay.
The original interpolated 16-bit angle behavior reads its 257-sample sine
table directly from the verified ROM rather than embedding retail data.
Recovered rotated-bound geometry uses the same angle path and preserves the
game's corner ordering for later collision and world-query integration.

`--window` runs the native gameplay bring-up through the pinned SDL2 host
library. It creates a native Linux or Windows window, maps keyboard and game
controllers to the original controller representation, and presents a native
pixel surface. It does not execute or translate N64 code. Its tank-following
bring-up view plots nearby decoded level-one retail placements and starts a
player tank from the decoded retail
group-zero spawn marker on open navigation data.
Drive with the left stick or WASD and fire the current cannon shell with the
left trigger or Left Shift. The default retail control preset keeps the turret
aligned with the hull. Movement and projectile travel stop at navigation
boundaries, blocked cells, and registered static world shapes. Escape or
closing the window exits.

Up to four hot-pluggable SDL game controllers are assigned to ports in
connection order. The left stick supplies the N64 analog stick, the D-pad and
shoulders map directly, the left trigger supplies Z, Start supplies Start, and
the right stick supplies the four C buttons. Keyboard controls remain available
for port one: WASD, X/Z, Left Shift, Enter, Q/E, arrows, and I/J/K/L.

## Saves

BattleTanx uses a Controller Pak rather than cartridge EEPROM. The native
runtime mounts `battletanx-pak1.mpk` in the SDL per-user application-data
directory. It is a standard 32 KiB Controller Pak filesystem image, compatible
with common N64 tools, containing BattleTanx's original 256-byte game file.
The game file's first big-endian word is the recovered retail checksum over
bytes 4 through 255. Reads reject bad filesystem metadata, size, chains, or
game checksums, and writes are read back and verified, matching the retail save
path's integrity checks. Campaign/menu integration will populate the recovered
record fields as those systems enter the port.

## Architecture

The original game uses big-endian, 32-bit N64 data and address tokens. The PC
runtime uses explicit-width integers and bounds-checked big-endian readers at
serialized boundaries. N64 addresses are never cast to native pointers.

Recovered gameplay behavior will enter the portable target in reviewed slices.
The matching N64 project remains a separate regression oracle and is not a PC
build configuration.

This project does not emulate the N64. The executable contains native source
implementations of recovered behavior and native platform services. It does
not contain a MIPS interpreter or translator, RSP/RDP emulation, generated CPU
translation, libultra compatibility runtime, or emulated N64 address space.
The verified ROM is opened strictly as a read-only source of retail game data.

## License

Project source is licensed under GPL-3.0-or-later. BattleTanx and its game data
remain the property of their respective owners.

