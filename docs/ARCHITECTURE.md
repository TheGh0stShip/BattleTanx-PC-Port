# Native-port architecture

The PC port and the matching Nintendo 64 decompilation are separate consumers
of the same recovered game behavior.

```text
recovered game behavior
        |
        +-- matching N64 build -> big-endian MIPS ROM
        |
        `-- portable game layer -> platform services -> Linux / Windows
```

The PC target does not execute MIPS instructions or dereference N64 virtual or
physical addresses. Recovered functions move into the portable game layer only
after their types, address use, serialized-data access, and platform
dependencies have been reviewed.

## Native-only gate

The PC executable is a source port, not an emulator. Its build rejects assembly,
generated translation sources, CPU-recompilation inputs, N64 instruction
interpreters, compatibility runtimes, and N64 renderer emulation dependencies.
The core currently has no linked runtime dependencies. The executable links
the reviewed core and platform layer; that platform layer links only pinned
SDL2 as its native window, framebuffer, keyboard, and controller service. Any
future audio or graphics dependencies must remain native host libraries and
must be reviewed at the build gate.

The user-supplied ROM is a read-only data container. Loading endian-sensitive
tables, compressed resources, models, textures, audio, or level data from it
does not execute cartridge code.

## Data boundary

- ROM and extracted-resource fields are explicitly sized and big-endian.
- Serialized 32-bit pointers remain address tokens until resolved within a
  validated resource arena.
- Native pointers are never narrowed to reproduce an N64 field.
- Runtime objects retain natural host alignment; serialized structures are
  decoded field by field instead of globally packing C++ types.
- The 22 world streams and 18 navigation streams are decompressed from audited
  ROM ranges into host-owned records. Their section boundaries, counts, shape
  references, vertex references, and adjacency indices are checked first.
- Recovered navigation queries operate on those host-owned records and retain
  the retail triangle orientation, blocked-kind lookup, and octagonal-distance
  behavior without reproducing the original address space. Straight-line route
  traversal uses serialized corner adjacency while preserving the retail
  248-edge termination bound.
- The gameplay bring-up owns native player-tank and projectile state. It uses
  the recovered controller, angle, planar-motion, and navigation rules without
  reproducing cartridge pointers or executing original machine code. The
  initial M1A1 slice uses its recovered retail drive constants; other vehicle
  tables remain outside this slice.
- Linux x86-64 is normally LP64, while Windows x64 is LLP64. Neither host data
  model implicitly reproduces the N64 o32 ABI.

## Platform boundary

Portable game code will depend on narrow interfaces for rendering, audio,
input, fixed-rate timing, storage, and resource access. Platform backends own
the corresponding Linux and Windows facilities.

## Bring-up order

1. Verify the user's USA Rev 0 ROM and expose bounds-checked resource views.
2. Add deterministic math, address-token, and resource-decoding tests.
3. Port a headless gameplay update slice with fixed input and timing.
4. Emit renderer-neutral scene commands from recovered behavior.
5. Add window and controller backends shared by Linux and Windows, followed by
   native audio and renderer backends.
6. Expand through menus, campaign, multiplayer, storage, and full-game
   regression coverage.

The executable is not considered playable until it can enter and complete
representative retail gameplay using only user-supplied ROM data.

