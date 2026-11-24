# Stack arguments in Mega Drive games (what the decompiler must model)

Measured on Sonic Spinball (23-11-2026, spinball-decomp `build/step7_stackaddr/`), where writing the decompiled C failed in
321 of 406 functions with "a stack address is used as a value". The compiler's convention is the SDK's, so every cartridge
built with it will show the same.

## The convention

- The caller pushes the arguments right to left (`move.l Dn,-(A7)`, `pea n.w`, `clr.l -(A7)`), calls (`jsr`/`bsr`) and drops
  them afterwards with `addq`/`lea n(A7),A7` (or a shared tail does).
- The callee starts with `movem.l Dn-Dn/An-An,-(A7)` and, without a frame, reads its arguments at `($n,A7)`, n past the saved
  registers; with `link A6,#-k` it reads them at `($n,A6)`, n >= 8, and its locals are at `(-n,A6)`.
- Results come back in D0. Registers D0/D1/A0/A1 are scratch; D2-D7/A2-A5 are saved by the callee, so a push of one at the
  start that is popped before `rts` is a saved register, not an argument.
- Some routines allocate a block with `lea (-n,A7),A7` and walk it through a copy of A7 (`movea.l A7,A0`): a local array.

## What it did to the decompiler (Sonic Spinball, 321 failing functions)

| Cause (per function, first match) | Functions |
|---|---|
| reads its arguments from the stack (`arg_read_sp`, `arg_read_frame`) | 229 |
| only pushes arguments for the calls it makes | 57 |
| only saves and restores a register or the SR | 26 |
| uses the address of its own buffer or array as a value | 9 |

Two layers, which matter for the fix:

1. **A gap in `decompiler/stack.py::slots()`** (core): the 68000 lift reaches memory through a temporary
   (`SetTemp t = SP+n; Load(t)`), and `slots()` follows temporaries, but `slot_reader()` and the `Store` lookup
   (`offset_of(statement.address, sp, None, frames)`) are not given them, so pushes and `($n,A7)` reads never become slots
   and end as `stack_address`. Passing `temps` to both makes 84 of the 321 write, 237 fail with another message
   (223 "reads its caller's stack at +N", 13 slots used at 16 and 32 bits, 1 return address) and leaves 9 real
   `stack_address` (the local buffers).
2. **The missing model:** with the gap closed the callers "write fine" and are wrong: `get_adjusted_ff000a` (0D4EFE) comes
   out as `multiply_and_divide_words();` because the pushed arguments are slots nothing reads (the callee's signature has
   register parameters only). A passing write is no proof; the arguments have to reach the call.

## What the decompiler needs

- Signatures: a slot `("s", +n, part)` read before written is a stack parameter; with `link A6` subtract the 8 bytes of the
  saved A6 and return address; with `movem` count what was saved.
- Calls: the bytes a callee reads and the caller drops afterwards are arguments of the `Call`, not dead slots; calls through a
  register take the arguments of each possible target.
- A push that is popped before `rts` (or into a register after a call) is a saved temporary: an ordinary local.
- The SR as a 16-bit slot (`move SR,-(A7)` / `move (A7)+,SR`).
- Local buffers (`lea (-n,A7),A7`, `lea (-n,A6,Dn.w),An`): a C array per function, `&local` for its address. The V70 has a
  fallback (`MEMORY_SP`, the stack kept in memory); the 68000 profile does not list register 32, so it is never used.

No other core CPU models stack arguments today: the V60 games pass nothing on the stack, and the Konami CPU refuses a read of
its caller's stack (`konami_cpu/decompile.py`). The data flow's `("s", offset, part)` variables are the function's own pushed
slots, not parameters.

Full per-cause counts, examples and how the sites were found: spinball-decomp session notes `stack_address_causes.md`.

## The model that was built (23-11-2026, arcade-decomp branch `stack-arguments`, 7098149)

`decompiler/stackargs.py`, off by default (profile flag `STACK_ARGUMENTS`): each argument a callee reads at `(n,A7)` /
`(n,A6)` is a virtual register of the data flow (`arg_n`, a word or a long as the callee reads it); the callee reads the
register (a slot copy of it where it reads it after a call of its own or writes it), the caller sets the registers of what
its callees read from the slots it pushed before every call and tail call (`Call.sp`), and the lift's two 16-bit stores of a
pushed long are one 32-bit slot. With the temporaries followed in `stack.slots()` (layer 1) the C writer goes from 321
functions it cannot write to 34 on Sonic Spinball, 206 of the 372 written take `arg_n` parameters, and the calls pass them
(checked against the asm on 10 caller/callee pairs). Left: 25 functions that use the address of a local buffer, 8 that use one
slot at two widths, 1 that reads its return address. Other games' C is byte for byte the same with the flag off.
Details and the lists: spinball-decomp session notes `stack_arguments_model.md`.

## What the 34 left do (24-11-2026, one worker task per function over the asm, spot-checked)

- **Address of a local handed to a callee (about 10):** an output word or long the callee fills (0E1D10 to 0DBC60, 0E7268
  to 0DB900, 0F3676, 0F4DFE and 0F4F2A to 0F3FE6, 0F56D0 and 0F5D02 to 0F42FE), and the palette fades' 0x20 to 0x100 byte
  buffers handed to 0F3AA4, 0F3AE0 and `fill_bytes` (0F3200, 0F3362, 0F34AE, 0F52EC, 0F5E0E). The callees read and write
  through the pointer with ordinary memory accesses, so the buffer must have a console address: a host C array does not fit.
- **A buffer walked in the function itself (the rest):** `movea.l A7,A0` or `lea (-n,A6),An` then `(An)+` / `(n,An)`
  (0F43E0, 0F4430, 0F4492 read the VDP into the stack and write it back; 0FE376, 0D61EA, 0DB900, 0F2F96, 0F3148).
- **One slot at two widths (8):** a long stored and its high word read back (0DBC60, 0DBCEA); a word whose high byte is
  changed with `andi.b`/`ori.b`/`bset` (0F49B8, 0F4AE4, 0F5122); and the outgoing argument area reused for another call at
  another width (0F2946, 0F4D2A, 0F5522: `subq.l #2,A7; move.w x,-(A7)` where a long was pushed before), which are two
  different variables at one offset.
- **0F6088 reads its return address:** the GEMS frame builder, whose every call FRAME_BUILDERS already puts in its sender.

The model's fallback for the first two is the V60's memory stack (`stack.MEMORY_SP`): the function's whole frame stays in the
console's RAM, so any address into it is a real one; on the 68000 it needs a bridge to the `arg_n` registers (push them on
entry, read the pushed ones back into `arg_n` before each call). It would also take the variable-size buffers the check
still refuses (0F3B3E, 0F5A7A, 0FFAAE, 0F47D2).

## The memory stack and the slots at two widths (24-11-2026, arcade-decomp branch `memory-stack`)

- **Frames in memory** (profile flag `MEMORY_STACK`, off by default): a function that still has a `stack_address` marker
  after `stack.slots()` is marked `memory_frame`. Its slots serve the data flow as before (the arguments become `arg_n`,
  `preserved.py` drops the `movem` saves), and only then `stack.to_memory()` puts its frame in the console's RAM: it
  lowers the global `stack_pointer` (register 32, `stack.MEMORY_SP`, starting at the reset vector's SSP) past the deepest
  place its stack reaches, each slot is `mem_read/mem_write(stack_pointer + n)`, a stack address is `stack_pointer + n`,
  and `stack_pointer` goes back up before each return. Callers and callees keep passing `arg_n` registers, so slot-mode and
  memory-mode functions mix. One that takes the address of its own arguments (0D6B88 `lea ($20,A7),A1` uses its first
  argument's slot as a 2-byte buffer) stores all of them in its frame first. Checked against the asm by one worker task per
  function (each `stack_pointer + n` against the `(d,A6)`/`(d,A7)` it stands for); the 0D6B88 case came from it. Putting the stack in RAM before the analysis (the V60's way) made every saved register a
  parameter of these functions (d2..a6), which is why the conversion comes last.
- **Slots at two widths** (`stackargs.widen()`, with `STACK_ARGUMENTS`): accesses of the function's own stack whose extents
  overlap are one slot of the whole extent (2 or 4 bytes), a narrower read is its part (big endian) and a narrower store
  keeps the rest. It runs before the calls' setups, so a pushed long with a byte stored inside it (0F4714) passes too. A
  frame that goes to memory is left as it is: there each access keeps the asm's width.
- **A LINK's saved A6 was marked as a stack address:** `slots()` replaced a frame register's reads with the frame's address
  using the frames at the end of the block, so `link A6` pushed "the new A6" instead of the caller's. Now each statement
  keeps the frames as they were when it was read. Nothing written changed (Spinball's C identical with the fix alone), but
  105 functions looked like they hand on a stack address; 44 really do.
- **Frame builders are not functions:** each call of one (FRAME_BUILDERS) is its instructions in the caller, so 0F6088 is
  no longer written (it read its return address).

On Sonic Spinball the C writer goes from 34 refused functions to 0 (405 written: 25 with their frame in RAM, 8 with joined
slots), and the check from 8 to 7: the functions whose SP the slots cannot follow (a buffer of computed size 0F3B3E, bytes
pushed in a loop 0FFAAE and 0F5A7A, script arguments pushed in a loop 0F47D2, pops deferred through shared code 0FA588, a
piece of a function named as one 0E6942, unreached code 0D54C8), with 102 callers behind them (76 behind `format_decimal`).

## The whole stack in memory, for SP the slots cannot follow (24-11-2026, arcade-decomp branch `whole-memory-stack`)

The 7 functions the check still refused were three different things:

- **SP the slots cannot follow (5):** bytes pushed in a loop and popped in another (0FFAAE `format_decimal`, 0F5A7A),
  script arguments pushed in a loop for a call through a register (0F47D2), a buffer of a size the code computes
  (0F3B3E: `suba.w D1, A7` twice), and calls whose arguments the compiler pops later, after ways meet (0FA588: 0FE2CC
  calls twice without popping and meets 0FE2B8's way at 0FE2E4, 8 bytes deeper; GCC's deferred pop). With
  `MEMORY_STACK` on, `stack.analyse()` no longer gives up when `slots()` does: `stack.whole()` keeps the function's
  whole stack in RAM from its start, as the 68000 does (SP is `stack_pointer`, every push, pop and `(n,A7)` access is
  `mem_read/mem_write`, A6 holds a real address). What the slots gave the data flow is found where SP is known,
  followed from the start where every way agrees, and counted back from each return (where SP must be where it started):
  the arguments it reads (`stackargs.in_memory()` lowers `stack_pointer` past them as the function starts and stores
  their `arg_n` registers there; before each call the callees' `arg_n` are read from `stack_pointer + n - 4`, as no
  return address is pushed; a jump to another function takes them from the function's own places), and the registers it
  saves as it starts and puts back from the same bytes on every way out, which stay slots for `preserved.py`. SP set from
  anything but SP or a frame is still an error, and so is a known SP that differs from what the ways out need.
- **A piece of a function named as one (0E6942):** the `jsr` in the middle of 0E65DA's code, which only the instruction
  before runs into. `LABELS_NOT_ENTRIES` now also takes a named place that only the previous instruction runs into (no
  call, no table, no computed jump; a frame builder's rest stays) as a place of the code that leads there. The code that
  leads there has an overlapping instruction (0E693E `ori.b #$12, D4` is the operand word of 0E693C's `pea $4.w` and of
  0E6940's `move.l (A2), -(A7)`), so where an overlapping instruction goes on now starts a block.
- **Unreached code (0D54C8):** the end of 0D527A past the jump that closes its loop, which puts back what 0D527A saved
  and returns with SP 20 bytes above where it started. No function of its own can have that stack: code symbols.txt
  names `unreached_` whose stack cannot be followed, and that nothing calls or jumps to, is left out of the C.

Also `stackargs.extract()`: three bytes (the rest of a long a byte is stored into, 0F4764 `move.b (A0)+, ($3,A7)`) are the
long masked, not a 24-bit value the data flow has no mask for; 0F4714 was behind format_decimal before.

On Sonic Spinball the check goes from 7 to 0 and the C writer writes all 512 functions (405 before; the 102 callers that
were left out are in). Checked against the asm: format_decimal and copy_vram_words line by line, 0FA588's entry, calls
and exit, and one worker task per function for the other four (all its mismatches were misreadings). TMNT's C is byte
for byte the same; The Simpsons runs no code this changes. What is left:

- 0F3B3E's saves of D1/A1/A2 are put back after `adda.w D1, A7` twice, at an SP the code computes, so they go through
  memory and the three registers are in/out parameters (following SP plus a value read from an argument would resolve it).
- A save by `move.l Dn,-(A7)` or `move.w` and its pop set the flags from the register, so `preserved.py` keeps them (the
  temporary that feeds the save also feeds the flags): format_decimal hands D2 back, 0F47D2 D7. The same holds in
  functions with slots (udiv32's D2 before this change); with the whole program written, 109 functions hand back a
  register their callers expect kept (23 before, of the 405).
- The data flow takes about 14 minutes for its first pass over the whole program, spread over the big functions
  it solves again and again (0D8F22, slots, 107 s in 38 runs; 0FA588, 19,208 statements, 93 s in 25), and writing the
  C about half an hour.
