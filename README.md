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
```

## Windows build

Open a Developer PowerShell for Visual Studio 2022, then run:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/BattleTanx.exe C:\Games\BattleTanx.z64
```

The current executable proves the portable build and ROM-loading boundary. It
validates the complete ROM, decompresses one audited retail resource bundle,
and validates its offset table before exposing entries to the native runtime.
The portable core also contains deterministic recovered controller-state and
planar-motion behavior covered by host tests.
The original interpolated 16-bit angle behavior reads its 257-sample sine
table directly from the verified ROM rather than embedding retail data.

## Architecture

The original game uses big-endian, 32-bit N64 data and address tokens. The PC
runtime uses explicit-width integers and bounds-checked big-endian readers at
serialized boundaries. N64 addresses are never cast to native pointers.

Recovered gameplay behavior will enter the portable target in reviewed slices.
The matching N64 project remains a separate regression oracle and is not a PC
build configuration.

## License

Project source is licensed under GPL-3.0-or-later. BattleTanx and its game data
remain the property of their respective owners.

