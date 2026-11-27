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
    game gives) is for the tools that run the original code only. Frame timing (`md/frames.h`): the way the
    translated and the decompiled program run, instructions taking no time and the console moving on to its
    next frame where the program waits for it (the game's frame waits: the branches back of the loops that
    spin until an interrupt). The translation scaffold of a game's 68000 program (`md_translated_program`):
    the C a game's `tools/recompile.py` writes (arcade-decomp's `m68000/translate.py`) on the 68000's runtime
    (`m68000/`, `src/m68000.c`, TMNT's on the console's bus; the tasks' stacks, `host/tasks.h`), and its
    headless runner (`src/headless.c`, mdref's options and hashes).
  - `reference/`: `mdref`, the original program on MAME's 68000 core against the runtime's console, with
    MAME's Z80 and PSG and ymfm's YM2612 (`sound/mdsound.cpp`). Per frame: hashes of the state, the picture
    and the sound, at the console's pace or with frame timing (`--timing frames`, as the translated program
    runs); and over a run: the code executed (`--trace`, `--calls`), where computed jumps went
    (`--jumps`), the cartridge's bytes read as data by the 68000 and by the Z80 (`--rom-reads`,
    `--z80-rom-reads`), dumps of the memories, the picture as BMP and the sound as WAV, and the load: the
    game's main loop steps, the frames it loses and the 68000 cycles of each function (`--step`, `--idle`,
    `--steps`, `--profile`). `reference.cmake` builds it. `mame/` holds MAME 0.289's Mega Drive VDP
    (315-5313 and the 315-5124 it is built on), its PSG (SN76496) and its driver (megadriv.cpp), as the
    description of the console.
  - `docs/`: what the model is built on, where each figure comes from (Sega's manuals and bulletins, MAME,
    BlastEm), and what the manuals say that the model does not do (`references.md`).

- `ngp/`: the SNK Neo Geo Pocket Color (the colour console, with a cartridge of up to 2 MB: flash chip 0).
  - `runtime/`: for now what the console's tools share: the control scripts the tests run (`ngp/script.h`:
    the pad, Option and the power button, and pokes of the RAM), and the reader of the player's cartridge
    and BIOS (`ngp_cartridge`, the Mega Drive's reader). The console's model in C comes next.
  - `reference/`: `ngpref`, the original program and the console's BIOS on MAME's TMP95C061 (`tlcs900/`),
    with MAME's Z80, T6W28 and the two DACs (`sound/ngpsound.cpp`) and MAME's K2GE
    (`video/k2gevideo.cpp`), wired as MAME's ngp.cpp wires them, without MAME's device framework: each on a
    stand-in for MAME's emu.h of its own. Time is the master clock (6.144 MHz, a dot of the screen), and the
    TMP95C061 and the Z80 run up to each of the console's events, as MAME's scheduler runs them. What MAME
    takes from the computer it runs on is fixed, so that runs repeat: the clock's date (`--rtc`) and the
    NVRAM (`--nvram`, `--save-nvram`: MAME's ngpc.nv). Per frame: hashes of the state, the picture and the
    sound; over a run: the code executed (`--trace`), where each JP or CALL through a register went
    (`--jumps`), the cartridge's bytes read as data (`--rom-reads`), every instruction disassembled (`--log`),
    dumps of the memories, the picture as BMP and the sound as WAV. `mame/` holds MAME 0.289's ngp.cpp, K1GE/K2GE and
    T6W28, as the description of the console; `flash.hxx` is ngp.cpp's flash commands as they are.
  - `docs/hardware.md`: the console as MAME 0.289 describes it, every fact cited to its file and line.
- `tlcs900/`: the Toshiba TLCS-900/H: MAME 0.289's core, its TMP95C061 and its disassembler (`mame/`), and the
  stand-in for MAME's emu.h they build on (`stub/`), unchanged but for one thing: the core's RDOP marks its
  instruction fetches, which go through the same space as its data (said in tlcs900.cpp's header).

The material read for reference (Sega's manuals, BlastEm's source, the GEMS research) lives in `external/`,
which git ignores: it is read, never committed.

## Status

- [x] The console's model in C, with the DMA and FIFO rates of Sega's Genesis Software Manual (p. 34) and the
      36 lines of V-Blank it gives for NTSC V28
- [x] `mdref`: boots and plays a cartridge (Sonic the Hedgehog Spinball) with its picture and sound, and
      measures its load per frame
- [x] The translation scaffold with frame timing: Spinball's 18 scenarios, the translated program against
      `mdref --timing frames`
- [ ] The reference's picture from MAME's 315-5313 (today the runtime's VDP draws it), so that the runtime's
      picture is checked against MAME's
- [ ] The timing checked against Nemesis's measures of the VDP's slots, against BlastEm running the same
      input, and against the console
- [ ] The VDP's shadow and highlight, interlace, the window plane's corner cases, six-button pads, PAL
- [x] Neo Geo Pocket Color: `ngpref` boots the real BIOS (a console never set up turns itself off until its
      power button, then asks for its first setup) and a cartridge (Sonic the Hedgehog Pocket Adventure) to its
      title and its first zone, with the same hashes run after run
- [x] `ngpref --jumps` and `--rom-reads`: MAME's TLCS-900 core fetches its instructions through the same reads
      as its data; its RDOP now marks them
- [ ] The Neo Geo Pocket Color's model in C (memory, the TMP95C061's timers and interrupts, the K2GE a line at a
      time, the T6W28 and the DACs, the flash, the pad) and its BIOS in C, checked against `ngpref`

## Using it from a game

The game's `CMakeLists.txt` includes `megadrive/runtime/runtime.cmake`, makes a library that reads its
cartridge (`game_rom_set_load` in `game/extract.h`, `game_program` in `game/data.h`, through
`md_cartridge_load`), sets `MD_GAME_DATA` to it and `MD_ARCADE_CORE` to its arcade-decomp, and includes
`megadrive/reference/reference.cmake`. It also gives the console its frame waits (`md_game_frame_waits` in a
source of its own, in `MD_GAME_DATA` too), and with a translated program in its build folder it calls
`md_translated_program(NAME GENERATED LIBRARIES...)`, which builds `NAME_headless`. The build needs Python 3 (MAME's 68000 and Z80 cores are written at
build time by MAME's own scripts) and a C++20 compiler.

A Neo Geo Pocket Color game does the same with `ngp/`: its `CMakeLists.txt` includes
`ngp/runtime/runtime.cmake`, makes a library that reads its cartridge and the BIOS (`game_rom_set_load` and
`game_bios_load` in `game/extract.h`, `game_program` and `game_bios` in `game/data.h`, through
`md_cartridge_load`, linked with `ngp_cartridge`), sets `NGP_GAME_DATA` to it and `NGP_ARCADE_CORE` to its
arcade-decomp, and includes `ngp/reference/reference.cmake`, which builds `ngpref`.

## Legal

- BSD 3-Clause License; see `LICENSE`.
- `megadrive/reference/mame/` holds copies of MAME's files (BSD-3-Clause, their copyright holders; see
  `megadrive/reference/mame/LICENSE`); they go into the reference tools only, never into a game.
  `megadrive/runtime/` follows the behavior of those files; the cores and ymfm are arcade-decomp's (BSD-3-Clause).
- Figures taken from Sega's documentation and from BlastEm are cited where they are used; none of that
  material is in this repository.
- No game's ROM, data or code is in this repository. Sega, Mega Drive and Genesis are trademarks of SEGA; this
  project is not affiliated with or endorsed by SEGA.
