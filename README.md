# console-decomp

Tools to decompile console games into readable C that does exactly what the original does, checked frame by
frame against MAME's cores: what a console adds to [arcade-decomp](https://github.com/cheviman/arcade-decomp)
(the decompiler, the CPUs' frontends, the static analysis and MAME's CPU cores, which this builds on and does
not copy). A console is a board every one of its games shares, so its model in C, its reference tool and its
documentation live here once, not in each game.

Each game keeps arcade-decomp in its `tools/core/` and this repository in its `tools/console/` (git
submodules), and says which console it is. Nothing here is a game's: no ROM, no names, no data. First used by
[spinball-decomp](https://github.com/cheviman/spinball-decomp) (Sonic the Hedgehog Spinball, Mega Drive).

## What is here

- `megadrive/`: the Sega Mega Drive / Genesis (NTSC, the first models: no TMSS, two three-button pads).
  - `runtime/`: the console as the program sees it, in C (`md_runtime`): the 68000's and the Z80's memory
    maps, the work RAM, the I/O ports and the pads, the Z80's bus and reset, the VDP (ports, registers, the
    three kinds of DMA, the interrupts, the H/V counters, and its picture a line at a time), and the control
    scripts the tests run (`md/script.h`). Time is in master clocks, and the model counts the time the 68000
    loses to the console: a DMA stops it, a full FIFO makes it wait, the DRAM refresh and the Z80's accesses
    to its bus take cycles from it. The reader of the player's cartridge (`md_cartridge`, by the CRC32 the
    game gives) is for the tools that run the original code only.
  - `reference/`: `mdref`, the original program on MAME's 68000 core against the runtime's console, with
    MAME's Z80 and PSG and ymfm's YM2612 (`sound/mdsound.cpp`). Per frame: hashes of the state, the picture
    and the sound; and over a run: the code executed (`--trace`, `--calls`), where computed jumps went
    (`--jumps`), the cartridge's bytes read as data by the 68000 and by the Z80 (`--rom-reads`,
    `--z80-rom-reads`), dumps of the memories, the picture as BMP and the sound as WAV, and the load: the
    game's main loop steps, the frames it loses and the 68000 cycles of each function (`--step`, `--idle`,
    `--steps`, `--profile`). `reference.cmake` builds it. `mame/` holds MAME 0.289's Mega Drive VDP
    (315-5313 and the 315-5124 it is built on), its PSG (SN76496) and its driver (megadriv.cpp), as the
    description of the console.
  - `docs/`: what the model is built on, where each figure comes from (Sega's manuals and bulletins, MAME,
    BlastEm), and what the manuals say that the model does not do (`references.md`).

The material read for reference (Sega's manuals, BlastEm's source, the GEMS research) lives in `external/`,
which git ignores: it is read, never committed.

## Status

- [x] The console's model in C, with the DMA and FIFO rates of Sega's Genesis Software Manual (p. 34) and the
      36 lines of V-Blank it gives for NTSC V28
- [x] `mdref`: boots and plays a cartridge (Sonic the Hedgehog Spinball) with its picture and sound, and
      measures its load per frame
- [ ] The reference's picture from MAME's 315-5313 (today the runtime's VDP draws it), so that the runtime's
      picture is checked against MAME's
- [ ] The timing checked against Nemesis's measures of the VDP's slots, against BlastEm running the same
      input, and against the console
- [ ] The VDP's shadow and highlight, interlace, the window plane's corner cases, six-button pads, PAL

## Using it from a game

The game's `CMakeLists.txt` includes `megadrive/runtime/runtime.cmake`, makes a library that reads its
cartridge (`game_rom_set_load` in `game/extract.h`, `game_program` in `game/data.h`, through
`md_cartridge_load`), sets `MD_GAME_DATA` to it and `MD_ARCADE_CORE` to its arcade-decomp, and includes
`megadrive/reference/reference.cmake`. The build needs Python 3 (MAME's 68000 and Z80 cores are written at
build time by MAME's own scripts) and a C++20 compiler.

## Legal

- BSD 3-Clause License; see `LICENSE`.
- `megadrive/reference/mame/` holds copies of MAME's files (BSD-3-Clause, their copyright holders; see
  `megadrive/reference/mame/LICENSE`); they go into the reference tools only, never into a game.
  `megadrive/runtime/` follows the behavior of those files; the cores and ymfm are arcade-decomp's (BSD-3-Clause).
- Figures taken from Sega's documentation and from BlastEm are cited where they are used; none of that
  material is in this repository.
- No game's ROM, data or code is in this repository. Sega, Mega Drive and Genesis are trademarks of SEGA; this
  project is not affiliated with or endorsed by SEGA.
