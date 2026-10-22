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

## Data boundary

- ROM and extracted-resource fields are explicitly sized and big-endian.
- Serialized 32-bit pointers remain address tokens until resolved within a
  validated resource arena.
- Native pointers are never narrowed to reproduce an N64 field.
- Runtime objects retain natural host alignment; serialized structures are
  decoded field by field instead of globally packing C++ types.
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
5. Add window, controller, audio, and renderer backends shared by Linux and
   Windows.
6. Expand through menus, campaign, multiplayer, storage, and full-game
   regression coverage.

The executable is not considered playable until it can enter and complete
representative retail gameplay using only user-supplied ROM data.

