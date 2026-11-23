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
