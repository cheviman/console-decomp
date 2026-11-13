# The Mega Drive's references

What the console model (`../runtime`) and the reference (`../reference`) are built on, besides MAME's code in
`../reference/mame`. The material itself, when it is downloaded, lives in `external/megadrive/` at the root of
this repository, which git ignores: manuals, scans and other people's code are read, never committed. What we
take from them (a timing, a register, a format) goes into these notes and the code's comments, with where it
came from (document and page, or file and line).

| What | Where it comes from | In external/megadrive/ |
|---|---|---|
| Sega's own manuals: the Genesis Software Manual (1990-02-06, 183 pages), the Genesis Software Development Manual 2.0 (1991-07-09), the Genesis Sound Software Manual (1992-02-21), the Technical Bulletins | segaretro.org/Mega_Drive_official_documentation (behind a bot check: downloaded by hand) | manuals/ |
| Nemesis's logic-analyzer measures of the VDP's access slots and the refresh | gendev.spritesmind.net forum ("VDP VRAM access timing", "Genesis 68K bus timing") | (forum pages) |
| Charles MacDonald's DMA rates (bytes a line by mode, width and display), as MAME's 315_5313.cpp quotes them | ../reference/mame/video/315_5313.cpp | — |
| BlastEm's source (GPL-3.0): the DRAM refresh (genesis.c: REFRESH_INTERVAL 128, REFRESH_DELAY 2), the Z80's accesses to the 68000's bus (genesis.c z80_read_bank: 3 Z80 cycles, 8 of the 68000, which its notes call an estimate), the VDP's slot tables (vdp.c) | retrodev.com/repos/blastem | blastem/ |
| GEMS (the sound driver many games use, Spinball among them): research and tools (LGPL/GPL) | github.com/realmonster/GEMS | gems-research/ |
| SGDK, a modern SDK with GCC for the 68000 (to build a game's C back into a ROM) | github.com/Stephane-D/SGDK | not yet |

## Taken into the model so far

| Figure | Value | From | In |
|---|---|---|---|
| Master clock, dividers | 53.693175 MHz; 68000 /7, Z80 /15, YM2612 /7 | MAME megadriv.cpp (MASTER_CLOCK_NTSC) | runtime/include/md/machine.h |
| Line, frame | 3420 master clocks; 262 lines, 224 displayed (NTSC, V28) | MAME 315_5313.cpp (NTSC table) | runtime/include/md/machine.h |
| DMA and FIFO rates | 68000 to VDP 16/167 (H32 displayed/blank), 18/205 (H40); fill 15/166, 17/204; copy 8/83, 9/102 | Sega, Genesis Software Manual p. 34 (the same table Charles MacDonald and MAME 315_5313.cpp give) | runtime/src/vdp.c |
| Units of a transfer | a VRAM word takes two byte slots; a CRAM or VSRAM word one | Genesis Software Manual p. 34; Genesis Technical Overview 1.00 p. 34 | runtime/src/vdp.c |
| Lines with the blanking's slots | all of them with the display off; with it on, 36 in V28 NTSC: 225-260 (line 224 keeps the displayed lines' slots, line 261 prepares line 0's sprites) | Genesis Software Manual p. 34 (36 lines, and DISP=0); BlastEm vdp.c (which lines) | runtime/src/vdp.c |
| FIFO | four words; a tight loop of writes in the displayed lines fills it and the 68000 waits (at most about 4.77 µs in H40, 5.96 µs in H32); never during V-Blank | Genesis Technical Overview 1.00 p. 34 | runtime/src/vdp.c (the waits evenly spread, not the slots' real places) |
| DRAM refresh | 2 cycles of the 68000 every 128 | BlastEm genesis.c | runtime/src/machine.c |
| Z80 on the 68000's bus | 8 cycles of the 68000, 3 of the Z80, each access | BlastEm genesis.c (an estimate, its notes say) | runtime/src/machine.c, reference/sound/mdsound.cpp |

## Read in Sega's manuals and not in the model

| What | Where | Why not |
|---|---|---|
| A read of VRAM needs more than 116 cycles of the 68000 (12 in V-Blank) before the next address is set, or it may read wrong | Genesis Technical Bulletin #15 (1993-04-05), PDF p. 37 | the model reads right whatever the wait; a program that breaks the rule reads what the console might not |
| Reading the pads ($A10000-$A100FF) with the Z80 running changes the access's wait state from 250 ns to 110 ns, and the Z80 misreads | Genesis Technical Bulletin #4 (1990-09-07), PDF p. 7 | a hardware fault the model does not have; programs ask for the Z80's bus around the read (Spinball's VBLANK does) |
| The 68000 accesses the Z80's RAM a byte at a time and with its bus | Genesis Technical Overview 1.00, "Z80 handshake" | the model allows a word access, as MAME does |

Not found in the manuals: the DRAM refresh's cost to the 68000 and the Z80's accesses to the 68000's bus, which
stay BlastEm's figures. Next: Nemesis's measures (the slots' places within a line), then BlastEm running the
same input, and the console.

The manuals are in `external/megadrive/manuals/` (git ignores it): Genesis Software Manual (rev. 02/20/92, and
the 1990-02-06 edition), Genesis Technical Bulletins, Genesis Technical Overview 1.00, Software Development
Manual 2.0, Sound Software Manual, the Quick Reference, the 6-button pad's specification, the service manuals,
the Super Mega Drive manual, SNASM68K's manuals, and a note on the SN76489. "p." is the manual's own page
number; "PDF p." the file's.
