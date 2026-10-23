# BattleTanx PC Port

This project is a native Linux and Windows source port of **BattleTanx** for
Nintendo 64 (USA Rev 0). It is under active development and is not yet a
complete recreation of every campaign system. The current build provides a
playable native combat slice using the first campaign level's recovered world,
unit placements, pickups, controller bindings, and save format.

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
./build/BattleTanx --validate /absolute/path/to/BattleTanx.z64
```

## Windows build

Open a Developer PowerShell for Visual Studio 2022, then run:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/BattleTanx.exe C:\Games\BattleTanx.z64
./build/BattleTanx.exe --validate C:\Games\BattleTanx.z64
```

## Install and portable packages

The executable has no bundled game data. Keep your legally obtained ROM
outside the source tree and pass its path when launching the game.

Install a locally built executable and this README into a directory of your
choice:

```sh
cmake --install build --prefix ./BattleTanx-PC
./BattleTanx-PC/BattleTanx /absolute/path/to/BattleTanx.z64
```

With a Visual Studio multi-configuration build on Windows, select the release
configuration explicitly:

```powershell
cmake --install build --config Release --prefix .\BattleTanx-PC
.\BattleTanx-PC\BattleTanx.exe C:\Games\BattleTanx.z64
```

To create the same ROM-free archive produced by continuous integration, run:

```sh
cmake --build build --target package --parallel
```

Configure with `-DBATTLETANX_PORTABLE=ON` to add the portable marker and a
`-portable` package suffix.

Linux packages are `.tar.gz` archives and Windows packages are `.zip`
archives. Each contains the executable, README, and license text. A SHA-256
sidecar is generated beside each archive. Extract the archive anywhere, then
launch `BattleTanx` (`BattleTanx.exe` on Windows) with the ROM path. Official
build artifacts are available from successful workflow runs; they
intentionally contain neither a ROM nor extracted retail assets.

The runtime remembers the last verified ROM, so subsequent launches need no
argument. Place an empty `portable.txt` beside the executable to keep settings
and the Controller Pak under a neighboring `userdata` directory. Otherwise it
uses the standard per-user data directory. `BATTLETANX_DATA_DIR` provides an
explicit override for scripted or shared installations.

The current executable implements the portable build and ROM-loading boundary.
It validates the complete ROM, decompresses all 21 audited retail resource
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
The portable core contains deterministic recovered controller-state and
planar-motion behavior covered by host tests. The default controller binding
preset is decoded from the verified retail ROM and evaluated with the recovered
analog/digital axis rules. The native battle session owns generation-checked
unit and projectile pools, player and computer control, target acquisition,
cannon fire, ammunition, damage, destruction, scoring, pickups, match results,
and respawning. The current M1A1 drive slice uses its recovered retail turn,
acceleration, grip, drag, and rotated footprint. Tank and shell collision uses
the recovered level-object type filter, each object's rotated level shape, and
the decoded navigation boundaries.
The current native presentation decodes and draws the retail world and tank
meshes with their textures through a host-owned software z-buffer. The tank
meshes and attached camera follow the decoded terrain height and surface normal.
The tank shadow uses its recovered translucent texture and render layer.
The camera contracts toward the tank when its eye path crosses a registered
static world footprint.
Several uncommon material modes remain bring-up work rather than a claim of
complete retail rendering fidelity.
The original interpolated 16-bit angle behavior reads its 257-sample sine
table directly from the verified ROM rather than embedding retail data.
Recovered rotated-bound geometry uses the same angle path and preserves the
game's corner ordering for later collision and world-query integration.
The native asset path now validates model sets, LOD ranges, parts, material and
geometry pool bounds, and decodes the retail vertex/triangle command stream.
Level-one assets are cached as host meshes and materials. Supported texture
decoders cover the five formats used by these assets: RGBA16, CI4 with TLUT,
RGBA32, I4, and I8. Recovered fixed, full-speed, and half-speed material-frame
selection drives texture scrolling and CI4 palette animation, including
independent tank tread phases.
No original display commands are executed at runtime.

The native audio path decodes all 49 retail sound effects from the verified
ROM into host PCM and mixes active one-shot voices through SDL at 44.1 kHz.
Gameplay fire, impact, destruction, pickup, and empty-ammunition events are
routed to this bounded stereo mixer, with simple listener-relative panning for
positional sounds. The first campaign track is rendered from its retail MIDI
sequence and 29-sample instrument bank, looped through the same native mixer,
and scaled by the original saved music-volume setting. The recovered looping
engine and skid voices respond to vehicle speed and stop with the player unit.

Normal launch runs the native gameplay runtime through the pinned SDL2 host
library. It creates a native Linux or Windows window, maps keyboard and game
controllers to the original controller representation, and presents a native
pixel surface. It does not execute or translate N64 code. Its recovered
tank-following camera renders decoded level-one retail placements and starts a
player tank from the decoded retail group-zero spawn marker on the decoded
navigation mesh. The same battle loads all three enemy tank placements and 51
pickup placements from that level's retail entity-bank group. Enemies acquire and
engage the player, cannon shells damage units and world obstacles, destroyed
tanks affect the mission result, and the player can respawn from the recovered
30 Hz match-state flow.

Press Start or Enter to begin. Drive with the left stick or WASD and hold
either trigger or Left Shift to fire the cannon at its original repeat
interval. Start or Enter pauses and resumes the match. The default retail
control preset keeps the turret aligned with the hull. Movement and projectile
travel stop at navigation boundaries, blocked cells, and registered static
world shapes. F11 or Alt+Enter toggles borderless fullscreen. Escape or
closing the window exits.

Host settings are stored in `settings.ini`. They cover window mode and size,
master/music/effects gain, controller deadzone and sensitivity, rumble
strength, and the remembered ROM path. Retail music/effects volume values in
the original Controller Pak remain active and are multiplied by the host gains.

The current playable slice is not yet the complete campaign. Remaining work
includes campaign menus and level transitions, recovered scripted enemy goals,
the remaining weapons and effects, full pickup behavior, the remaining music
selection paths, and several uncommon render modes. The player tank now uses
the retail looping engine and skid sounds with speed-driven pitch and volume.

Up to four hot-pluggable SDL game controllers are assigned to ports in
connection order. The left stick supplies the N64 analog stick, the D-pad and
shoulders map directly, either trigger supplies Z, the south face button
supplies A, either adjacent face button supplies B, Start supplies Start, and
the right stick supplies the four C buttons. Keyboard controls remain
available for port one: WASD, X/Z, Left Shift, Enter, Q/E, arrows, and
I/J/K/L.

## Saves

BattleTanx uses a Controller Pak rather than cartridge EEPROM. The native
runtime mounts `battletanx-pak1.mpk` in its per-user or portable data
directory. It is a standard 32 KiB Controller Pak filesystem image, compatible
with common N64 tools, containing BattleTanx's original 256-byte game file.
The game file's first big-endian word is the recovered retail checksum over
bytes 4 through 255. Reads reject bad filesystem metadata, size, chains, or
game checksums, and writes are read back and verified, matching the retail save
path's integrity checks. The runtime parses the recovered campaign fields,
preserves unknown bytes, applies saved unlimited-ammunition state, and writes
the updated mission and score record after a successful battle.

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

