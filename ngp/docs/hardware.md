# Neo Geo Pocket Color: the hardware, from MAME 0.289
## Memory map (TLCS-900H)

| Address Range | Component / Function | Read Handler | Write Handler | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0x000080` - `0x0000BF` | NGP/NGPC Specific I/O | `ngp_state::io_r` | `ngp_state::io_w` | `ngp.cpp:560` |
| `0x004000` - `0x006FFF` | Work RAM (Shared) | RAM | RAM | `ngp.cpp:561` |
| `0x007000` - `0x007FFF` | Shared RAM (Sound CPU) | RAM | RAM | `ngp.cpp:562` |
| `0x008000` - `0x00BFFF` | Video Chip (K1GE/K2GE) | `k1ge_device::read` | `k1ge_device::write` | `ngp.cpp:563` |
| `0x200000` - `0x3FFFFF` | Cartridge Area #1 (Flash 0) | ROM (installed at start) | `ngp_state::flash0_w` | `ngp.cpp:564, 665` |
| `0x800000` - `0x9FFFFF` | Cartridge Area #2 (Flash 1) | ROM (installed at start) | `ngp_state::flash1_w` | `ngp.cpp:565, 666` |
| `0xFF0000` - `0xFFFFFF` | System ROM (BIOS) | ROM Region "maincpu" | N/A | `ngp.cpp:566` |

*   ROMs for cartridge areas are installed at `machine_start` (`ngp.cpp:665-666`) and unmapped if no cart is present (`ngp.cpp:706-707`).

## NGP I/O (0x80-0xBF)

**Joypad & Power**

| Address | Offset | Function | Behavior | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0xB0` (io 0x30) | Controls | Read: `m_io_controls->read()`. Bits: `0x01` Up, `0x02` Down, `0x04` Left, `0x08` Right, `0x10` A, `0x20` B, `0x40` Option, `0x80` Unused. | `ngp.cpp:252, 621-628` |
| `0xB1` (io 0x31) | Power/Battery | Read: `(m_io_power->read() & 0x01) \| 0x02`. Bit `0x01` Power state, Bit `0x02` Sub-battery OK (hardcoded 1). | `ngp.cpp:254-256` |

**Z80 Link & Communication**

| Address | Offset | Function | Behavior | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0xB9` (io 0x39) | Z80 Enable/Disable | Write `0x55`: Enable (Resume from HALT, reset Z80, clear IRQ0). Write `0xAA`: Disable (Suspend with HALT). | `ngp.cpp:316-322` |
| `0xBA` (io 0x3A) | Z80 NMI | Write: Pulse Z80 NMI line. | `ngp.cpp:328` |
| `0xBC` (io 0x3C) | Z80 Comm | Read/Write: `m_io_reg[0x3c]`. Z80 side is at Z80 Mem `0x8000`. | `ngp.cpp:572, 578, 592` |

**Sound (T6W28 & DAC)**

| Address | Offset | Function | Behavior | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0xB8` (io 0x38) | Sound Enable | Write `0x55`: Enable T6W28. Write `0xAA`: Disable T6W28. | `ngp.cpp:304-308` |
| `0xA0` (io 0x20) | T6W28 Right | Write: If `io_reg[0x38]==0x55` and `io_reg[0x39]==0xAA`, writes to T6W28 port 0. | `ngp.cpp:284-288` |
| `0xA1` (io 0x21) | T6W28 Left | Write: If `io_reg[0x38]==0x55` and `io_reg[0x39]==0xAA`, writes to T6W28 port 0. | `ngp.cpp:284-288` |
| `0xA2` (io 0x22) | DAC Right | Write: Writes to `m_rdac`. | `ngp.cpp:291-292` |
| `0xA3` (io 0x23) | DAC Left | Write: Writes to `m_ldac`. | `ngp.cpp:294-295` |

**Real-Time Clock (RTC)**

| Address | Offset | Function | Behavior | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0x91` (io 0x11) | Year | Read: BCD `RTC_YEAR % 100`. Write: Set `RTC_YEAR` from BCD. | `ngp.cpp:240, 268` |
| `0x92` (io 0x12) | Month | Read: BCD `RTC_MONTH`. Write: Set `RTC_MONTH` from BCD. | `ngp.cpp:242, 271` |
| `0x93` (io 0x13) | Day | Read: BCD `RTC_DAY`. Write: Set `RTC_DAY` from BCD. | `ngp.cpp:244, 274` |
| `0x94` (io 0x14) | Hour | Read: BCD `RTC_HOUR`. Write: Set `RTC_HOUR` from BCD. | `ngp.cpp:246, 277` |
| `0x95` (io 0x15) | Minute | Read: BCD `RTC_MINUTE`. Write: Set `RTC_MINUTE` from BCD. | `ngp.cpp:248, 280` |
| `0x96` (io 0x16) | Second | Read: BCD `RTC_SECOND`. Write: Set `RTC_SECOND` from BCD. | `ngp.cpp:250, 283` |

**System/Power Down**

| Address | Offset | Function | Behavior | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0xB6` (io 0x36) | Power Down/Start | Write: Ignored. | `ngp.cpp:299` |
| `0xB7` (io 0x37) | Unknown | Write: Ignored. | `ngp.cpp:299` |

## The Z80 and the link

**Memory Map**

| Address Range | Component / Function | Read Handler | Write Handler | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0x0000` - `0x0FFF` | Shared RAM (Main CPU) | RAM | RAM | `ngp.cpp:590` |
| `0x4000` - `0x4001` | T6W28 Sound Chip | N/A | `t6w28_device::write` | `ngp.cpp:591` |
| `0x8000` - `0x8000` | Main-Sound Comm | `ngp_state::z80_comm_r` | `ngp_state::z80_comm_w` | `ngp.cpp:592` |
| `0xC000` - `0xC000` | Signal IRQ to Main | N/A | `ngp_state::z80_signal_main_w` | `ngp.cpp:593` |

**I/O Ports**

| Port Range | Function | Handler | Source |
| :--- | :--- | :--- | :--- |
| `0x0000` - `0xFFFF` | Clear IRQ | `ngp_state::z80_clear_irq` | `ngp.cpp:608` |

*   `z80_clear_irq`: Clears Z80 IRQ line 0 and TLCS-900 INT5 (`ngp.cpp:599-602`).
*   `z80_signal_main_w`: Asserts TLCS-900 INT5 (`ngp.cpp:584`).

## TMP95C061: interrupts

**Registers**

| Address | Name | Bits | Function | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0x70` | `INTE0AD` | 3, 7 | INT0, A/D End | `tmp95c061.cpp` |
| `0x71` | `INTE45` | 3, 7 | INT4, INT5 | `tmp95c061.cpp` |
| `0x72` | `INTE67` | 3, 7 | INT6, INT7 | `tmp95c061.cpp` |
| `0x73` | `INTET10` | 3, 7 | Timer 0, Timer 1 | `tmp95c061.cpp` |
| `0x74` | `INTET32` | 3, 7 | Timer 2, Timer 3 | `tmp95c061.cpp` |
| `0x75` | `INTET54` | 3, 7 | Timer 4, Timer 5 | `tmp95c061.cpp` |
| `0x76` | `INTET76` | 3, 7 | Timer 6, Timer 7 | `tmp95c061.cpp` |
| `0x77` | `INTES0` | 3, 7 | Serial 0 RX, Serial 0 TX | `tmp95c061.cpp` |
| `0x78` | `INTES1` | 3, 7 | Serial 1 RX, Serial 1 TX | `tmp95c061.cpp` |
| `0x79` | `INTETC10` | 3, 7 | HDMA 0, HDMA 1 | `tmp95c061.cpp` |
| `0x7A` | `INTETC32` | 3, 7 | HDMA 2, HDMA 3 | `tmp95c061.cpp` |
| `0x7B` | `IIMC` | 1, 2 | INT0 Edge Trigger, INT0 Enable | `tmp95c061.cpp` |

**Priority & Selection**

*   Priority levels 0-7 determined by index in `irq_vectors` (`tmp95c061.cpp:518`).
*   Sources with `iff` bit 7 (0x80) map to priority `(reg >> 4) & 0x07` (`tmp95c061.cpp:526`).
*   Sources with `iff` bit 3 (0x08) map to priority `reg & 0x07` (`tmp95c061.cpp:529`).
*   Loop goes from level `max(1, IFF)` to 6, keeping the highest pending source (`tmp95c061.cpp:532`).
*   Level taken is `i + 1` (`tmp95c061.cpp:538`).
*   CPU pushes PC (4 bytes) then SR (2 bytes) on XSSP (`tmp95c061.cpp:532-567`).
*   Sets IFF to that level (`tmp95c061.cpp:558`).
*   Reads new PC from `0xFFFF00 + vector` (`tmp95c061.cpp:560`).
*   Adds 18 cycles (`tmp95c061.cpp:532-567`).
*   Clears source's request bit by ANDing interrupt register with inverse of `iff` mask (`tmp95c061.cpp:567`).

**Vectors**

| Source | Vector Offset | Source |
| :--- | :--- | :--- |
| INTTC3 (HDMA 3) | `0x80` | `tmp95c061.cpp:324` |
| INTTC2 (HDMA 2) | `0x7C` | `tmp95c061.cpp:325` |
| INTTC1 (HDMA 1) | `0x78` | `tmp95c061.cpp:326` |
| INTTC0 (HDMA 0) | `0x74` | `tmp95c061.cpp:327` |
| INTAD | `0x70` | `tmp95c061.cpp:328` |
| INTTX1 | `0x6C` | `tmp95c061.cpp:329` |
| INTRX1 | `0x68` | `tmp95c061.cpp:330` |
| INTTX0 | `0x64` | `tmp95c061.cpp:331` |
| INTRX0 | `0x60` | `tmp95c061.cpp:332` |
| INTTR7 | `0x5C` | `tmp95c061.cpp:333` |
| INTTR6 | `0x58` | `tmp95c061.cpp:334` |
| INTTR5 | `0x54` | `tmp95c061.cpp:335` |
| INTTR4 | `0x50` | `tmp95c061.cpp:336` |
| INTT3 | `0x4C` | `tmp95c061.cpp:337` |
| INTT2 | `0x48` | `tmp95c061.cpp:338` |
| INTT1 | `0x44` | `tmp95c061.cpp:339` |
| INTT0 | `0x40` | `tmp95c061.cpp:340` |
| INT7 | `0x38` | `tmp95c061.cpp:342` |
| INT6 | `0x34` | `tmp95c061.cpp:343` |
| INT5 | `0x30` | `tmp95c061.cpp:344` |
| INT4 | `0x2C` | `tmp95c061.cpp:345` |
| INT0 | `0x28` | `tmp95c061.cpp:346` |

**Triggering**

*   INT0: Edge if `IIMC` bit 1 set, else Level (`tmp95c061.cpp:854`).
*   INT4: Edge if `PBCR` bit 0 clear (`tmp95c061.cpp:877`).
*   INT5: Edge if `PBCR` bit 1 clear (`tmp95c061.cpp:888`).
*   A/D End cleared by reading upper byte of ADREG (`tmp95c061.cpp:1241`).
*   Serial TX flags set on write to SCBUF (`tmp95c061.cpp:1139`).

## TMP95C061: timers

**Registers**

| Address | Name | Function | Source |
| :--- | :--- | :--- | :--- |
| `0x20` | `TRUN` | Timer Run Control | `tmp95c061.cpp:1364` |
| `0x22` | `TREG0` | Timer 0 Counter | `tmp95c061.cpp:1365` |
| `0x23` | `TREG1` | Timer 1 Counter | `tmp95c061.cpp:1365` |
| `0x24` | `T01MOD` | Timer 0/1 Mode | `tmp95c061.cpp:1365` |
| `0x25` | `TFFCR` | Timer Flip-Flop Control | `tmp95c061.cpp:1365` |
| `0x26` | `TREG2` | Timer 2 Counter | `tmp95c061.cpp:1366` |
| `0x27` | `TREG3` | Timer 3 Counter | `tmp95c061.cpp:1366` |
| `0x28` | `T23MOD` | Timer 2/3 Mode | `tmp95c061.cpp:1366` |
| `0x29` | `TRDC` | Timer Divider Control | `tmp95c061.cpp:1366` |

**Clock Sources & Prescaler**

*   Prescaler active when `TRUN` bit 7 (0x80) is set (`tmp95c061.cpp:672`).
*   Timer 0: `T1` (`pre>>7`), `T4` (`pre>>9`), `T16` (`pre>>11`) (`tmp95c061.cpp:683-689`).
*   Timer 1: `T1` (`pre>>7`), `T16` (`pre>>11`), `T256` (`pre>>15`) (`tmp95c061.cpp:721-727`).
*   Timer 2: `T1` (`pre>>7`), `T4` (`pre>>9`), `T16` (`pre>>11`) (`tmp95c061.cpp:760-766`).
*   Timer 3: `T1` (`pre>>7`), `T16` (`pre>>11`), `T256` (`pre>>15`) (`tmp95c061.cpp:798-804`).

**Modes & Counting**

*   Timer 0 Mode bits 0-1: `00`=TIO, `01`=T1, `02`=T4, `03`=T16 (`tmp95c061.cpp:678-690`).
*   Timer 1 Mode bits 2-3: `00`=TO0TRG, `01`=T1, `02`=T16, `03`=T256 (`tmp95c061.cpp:716-728`).
*   Timer 2 Mode bits 0-1: `01`=T1, `02`=T4, `03`=T16 (`tmp95c061.cpp:756-767`).
*   Timer 3 Mode bits 2-3: `00`=TO2TRG, `01`=T1, `02`=T16, `03`=T256 (`tmp95c061.cpp:793-805`).
*   Counts up: `m_timer_8[i] += 1` per tick (`tmp95c061.cpp:695`).
*   Match: `m_timer_8[i] == m_t8_reg[i]` (`tmp95c061.cpp:696`).
*   16-bit mode: `T01MOD`/`T23MOD` bits 6-7 == 0x40 (`tmp95c061.cpp:704, 781`).
*   16-bit: Lower timer (0/2) does not reset on match; Upper timer (1/3) resets lower on match (`tmp95c061.cpp:704-708, 745-748, 821-825`).
*   Timer 0 triggers Timer 1 if `TRUN` bit 1 set and `T01MOD` bits 2-3 == 0x00 (`tmp95c061.cpp:698-701`).
*   Timer 2 triggers Timer 3 if `TRUN` bit 3 set and `T23MOD` bits 2-3 == 0x00 (`tmp95c061.cpp:775-778`).

**Outputs (TO1/TO3)**

*   TO1 controlled by `TFFCR` bits 2-3: `00`=Invert, `04`=Set, `08`=Clear (`tmp95c061.cpp:964-975`).
*   TO3 controlled by `TFFCR` bits 6-7: `00`=Invert, `40`=Set, `80`=Clear (`tmp95c061.cpp:976-987`).
*   TO1 inverted on Timer 1 match if `TFFCR` bit 1 set (`tmp95c061.cpp:739-742`).
*   TO3 inverted on Timer 3 match if `TFFCR` bit 5 set (`tmp95c061.cpp:816-819`).

**Interrupts**

*   Timer 0 match sets `INTET10` bit 3 (0x08) (`tmp95c061.cpp:707`).
*   Timer 1 match sets `INTET10` bit 7 (0x80) (`tmp95c061.cpp:737`).
*   Timer 2 match sets `INTET32` bit 3 (0x08) (`tmp95c061.cpp:784`).
*   Timer 3 match sets `INTET32` bit 7 (0x80) (`tmp95c061.cpp:814`).

## TMP95C061: micro DMA

**Start Vector Registers**

| Address | Name | Function | Source |
| :--- | :--- | :--- | :--- |
| `0x7C` | `DMA0V` | Channel 0 Source | `tmp95c061.cpp:1391` |
| `0x7D` | `DMA1V` | Channel 1 Source | `tmp95c061.cpp:1391` |
| `0x7E` | `DMA2V` | Channel 2 Source | `tmp95c061.cpp:1391` |
| `0x7F` | `DMA3V` | Channel 3 Source | `tmp95c061.cpp:1391` |

*   Vector value: `(reg & 0x1f) << 2` (`tmp95c061.cpp:353`).
*   Valid vectors: `0x28` to `0x72` (excluding `0x3C`) (`tmp95c061.cpp:356`).

**Channel Registers**

| Address | Name | Function | Source |
| :--- | :--- | :--- | :--- |
| `0x20` | `DMAC0` | Count Ch 0 | `tmp95c061.cpp:1396` |
| `0x22` | `DMAM0` | Mode Ch 0 | `tmp95c061.cpp:1395` |
| `0x00` | `DMAS0` | Source Ch 0 | `tmp95c061.cpp:1397` |
| `0x10` | `DMAD0` | Dest Ch 0 | `tmp95c061.cpp:1398` |
| `0x24` | `DMAC1` | Count Ch 1 | `tmp95c061.cpp:1396` |
| `0x26` | `DMAM1` | Mode Ch 1 | `tmp95c061.cpp:1395` |
| `0x04` | `DMAS1` | Source Ch 1 | `tmp95c061.cpp:1397` |
| `0x14` | `DMAD1` | Dest Ch 1 | `tmp95c061.cpp:1398` |
| `0x28` | `DMAC2` | Count Ch 2 | `tmp95c061.cpp:1396` |
| `0x2A` | `DMAM2` | Mode Ch 2 | `tmp95c061.cpp:1395` |
| `0x08` | `DMAS2` | Source Ch 2 | `tmp95c061.cpp:1397` |
| `0x18` | `DMAD2` | Dest Ch 2 | `tmp95c061.cpp:1398` |
| `0x2C` | `DMAC3` | Count Ch 3 | `tmp95c061.cpp:1396` |
| `0x2E` | `DMAM3` | Mode Ch 3 | `tmp95c061.cpp:1395` |
| `0x0C` | `DMAS3` | Source Ch 3 | `tmp95c061.cpp:1397` |
| `0x1C` | `DMAD3` | Dest Ch 3 | `tmp95c061.cpp:1398` |

**Transfer Modes**

| Mode | Function | Source |
| :--- | :--- | :--- |
| `0x00` | Byte copy, Dest Inc | `tmp95c061.cpp:368` |
| `0x01` | Word copy, Dest Inc | `tmp95c061.cpp:373` |
| `0x02` | Long copy, Dest Inc | `tmp95c061.cpp:378` |
| `0x04` | Byte copy, Dest Dec | `tmp95c061.cpp:383` |
| `0x05` | Word copy, Dest Dec | `tmp95c061.cpp:388` |
| `0x06` | Long copy, Dest Dec | `tmp95c061.cpp:393` |
| `0x08` | Byte copy, Src Inc | `tmp95c061.cpp:398` |
| `0x09` | Word copy, Src Inc | `tmp95c061.cpp:403` |
| `0x0A` | Long copy, Src Inc | `tmp95c061.cpp:408` |
| `0x0C` | Byte copy, Src Dec | `tmp95c061.cpp:413` |
| `0x0D` | Word copy, Src Dec | `tmp95c061.cpp:418` |
| `0x0E` | Long copy, Src Dec | `tmp95c061.cpp:423` |
| `0x10` | Byte copy, Fixed Addr | `tmp95c061.cpp:428` |
| `0x11` | Word copy, Fixed Addr | `tmp95c061.cpp:432` |
| `0x12` | Long copy, Fixed Addr | `tmp95c061.cpp:436` |
| `0x14` | Src Inc only (no data) | `tmp95c061.cpp:440` |

**Execution**

*   Runs when CPU interrupt mask is NOT 7: `(SR.b.h & 0x70) != 0x70` (`tmp95c061.cpp:479`).
*   Channels processed 0, 1, 2, 3 (`tmp95c061.cpp:483`).
*   Source interrupt flag cleared after transfer (`tmp95c061.cpp:469`).
*   Count register decremented after transfer (`tmp95c061.cpp:446`).
*   When count == 0: Vector reg cleared to 0, HDMA end interrupt flag set (`tmp95c061.cpp:450, 454`).

## TLCS-900H core: registers, SR, interrupts taken, cycles

**Registers**

*   Banks: `m_xwa[4]`, `m_xbc[4]`, `m_xde[4]`, `m_xhl[4]` (`tlcs900.h:88-91`).
*   Special: `m_xix`, `m_xiy`, `m_xiz` (32-bit), `m_xssp` (Sys SP), `m_xnsp` (Norm SP), `m_pc`, `m_sr`, `m_f2` (`tlcs900.h:92-99`).
*   DMA: `m_dmas[4]`, `m_dmad[4]`, `m_dmac[4]`, `m_dmam[4]` (`tlcs900.h:101-104`).
*   Bank switching: `m_regbank` derived from SR RFP bits (`tlcs900.h:133`).

**Status Register (SR)**

| Bit | Mask | Name | Description | Source |
| :--- | :--- | :--- | :--- | :--- |
| 15 | `0x8000` | S | System Mode (1) / User Mode (0) | `tlcs900.cpp:248` |
| 14-12 | `0x7000` | IFF | Interrupt Flag bits | `tlcs900.cpp:249` |
| 11 | `0x0800` | M | Max Mode (1) / Min Mode (0) | `tlcs900.cpp:250` |
| 10-8 | `0x0700` | RFP | Register File Pointer (Bank 0-3) | `tlcs900.cpp:251` |
| 7 | `0x0080` | S | Sign Flag | `tlcs900.cpp:252` |
| 6 | `0x0040` | Z | Zero Flag | `tlcs900.cpp:253` |
| 5 | `0x0020` | - | Unused | `tlcs900.cpp:254` |
| 4 | `0x0010` | H | Half Carry Flag | `tlcs900.cpp:255` |
| 3 | `0x0008` | - | Unused | `tlcs900.cpp:256` |
| 2 | `0x0004` | V | Overflow Flag | `tlcs900.cpp:257` |
| 1 | `0x0002` | N | Negative Flag | `tlcs900.cpp:258` |
| 0 | `0x0001` | C | Carry Flag | `tlcs900.cpp:259` |

*   Constants: `FLAG_CF`=0x01, `FLAG_NF`=0x02, `FLAG_VF`=0x04, `FLAG_HF`=0x10, `FLAG_ZF`=0x40, `FLAG_SF`=0x80 (`tlcs900.cpp:73-78`).
*   F' Register: `m_f2` (`tlcs900.cpp:135`).

**Interrupts Taken**

*   Checked at start of instruction if `m_check_irqs` set (`tlcs900.cpp:277-293`).
*   `m_irq_inhibit` defers by one instruction after EI/RETI (`tlcs900.cpp:279-287`).
*   Push: PC pushed to stack (XSSP or XNSP) (`tlcs900.cpp`).
*   RETI: Pops PC, clears IFF0 (`tlcs900.h:492`).

**Cycles (TLCS-900H)**

| Operation | Cycles | Source |
| :--- | :--- | :--- |
| GPR | 1 | `tlcs900.h:653` |
| Mem Index | 1 | `tlcs900.h:654` |
| Mem Abs 8 | 1 | `tlcs900.h:655` |
| Mem Abs 16 | 2 | `tlcs900.h:656` |
| Mem Abs 24 | 3 | `tlcs900.h:657` |
| Mem GPR Indirect | 1 | `tlcs900.h:658` |
| Mem GPR Index | 3 | `tlcs900.h:659` |
| Mem GPR Reg Index | 3 | `tlcs900.h:660` |
| Mem Indirect Pre/Post | 1 | `tlcs900.h:661` |
| LDXX Repeat | -1 | `tlcs900.h:662` |
| JP True | 3 | `tlcs900.h:663` |
| CALL True | 8 | `tlcs900.h:664` |
| DJNZ True | 2 | `tlcs900.h:665` |
| Shift(n) | n / 4 | `tlcs900.h:666` |
| Halt | 8 | `tlcs900.cpp:302` |

*   Reset Vector: Read from `0xFFFF00`-`0xFFFF02` (`tlcs900.cpp:215-218`).
*   SR Reset: `0xF800` (Max, System) (`tlcs900.cpp:220`).

## K2GE video: registers

Addresses in the K2GE sections are offsets in the chip: the CPU sees them at 0x8000 + offset.

**General Registers**

| Address | Name | Bits | Meaning | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0x000` | Interrupt Enable | 7 | VBlank Int Enable | `k1ge.cpp:71, 382-385` |
| | | 6 | HBlank Int Enable | `k1ge.cpp:407-410` |
| `0x002` | WBA.H | 0-7 | Window BG Area X Start | `k1ge.cpp:355-358` |
| `0x003` | WBA.V | 0-7 | Window BG Area Y Start | `k1ge.cpp:308-313` |
| `0x004` | WSI.H | 0-7 | Window Size X | `k1ge.cpp:360-363` |
| `0x005` | WSI.V | 0-7 | Window Size Y | `k1ge.cpp:308` |
| `0x007` | REF | 0-7 | Reference Register (Reset 0xC6) | `k1ge.cpp:466` |
| `0x008` | RAS.H | 0-7 | Read Only: `screen().hpos() >> 2` | `k1ge.cpp:53-55` |
| `0x009` | RAS.V | 0-7 | Read Only: `screen().vpos()` | `k1ge.cpp:56-58` |
| `0x010` | Status | 6 | VBlank Flag | `k1ge.cpp:381, 395` |
| `0x012` | OOW Color | 0-2 | Out-of-Window Color | `k1ge.cpp:73-75, 108-110` |
| `0x020` | PO.H | 0-7 | Sprite Origin X | `k1ge.cpp:260, 468` |
| `0x021` | PO.V | 0-7 | Sprite Origin Y | `k1ge.cpp:261, 469` |
| `0x030` | PF | 7 | Plane Flip / Priority Swap | `k1ge.cpp:320-353` |
| `0x032` | S1SO.H | 0-7 | Scroll 1 X | `k1ge.cpp:326, 349` |
| `0x033` | S1SO.V | 0-7 | Scroll 1 Y | `k1ge.cpp:326, 349` |
| `0x034` | S2SO.H | 0-7 | Scroll 2 X | `k1ge.cpp:332, 343` |
| `0x035` | S2SO.V | 0-7 | Scroll 2 Y | `k1ge.cpp:332, 343` |
| `0x400` | LED Control | 0-7 | LED Control | `k1ge.cpp:494` |
| `0x402` | LEDFREG | 0-7 | LED Frame | `k1ge.cpp:495` |
| `0x7E0` | RESET | 0-7 | Reset Register | `k1ge.cpp:496` |
| `0x7E2` | MODE | 7 | K2GE Compat Mode (1=K1GE) | `k1ge.cpp:123-128` |

**Palette Registers**

| Address Range | Name | Bits | Meaning | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0x100` - `0x117` | Sprite/Plane Palettes | 0-2 | K1GE: Indirect palette. K2GE: Indirect palette + mono offset. | `k1ge.cpp:79-87, 111-119` |
| `0x118` | BG Color | 7 | BG Enable. If set, uses `data & 7` (K1GE) or `mono+0x30+(data&7)` (K2GE). | `k1ge.cpp:88-90, 120-122` |
| `0x200` - `0x3FF` | K2GE RGB Palette | - | 256 entries, 12-bit RGB. Low byte `0x2xx`, High byte `0x3xx`. | `k1ge.cpp:143-150` |

## K2GE video: memory layout

**VRAM Size:** 0x4000 bytes (`k1ge.cpp:440`)

| Region | Address Range | Size | Format / Description | Source |
| :--- | :--- | :--- | :--- | :--- |
| **Sprite Table** | `0x0800` - `0x08FF` | 256 bytes | 64 entries (4 bytes each). | `k1ge.cpp:254-258` |
| | | | `0x0800+i`: Data Low | |
| | | | `0x0801+i`: Data High | |
| | | | `0x0802+i`: X Offset / Chain | |
| | | | `0x0803+i`: Y Offset / Chain | |
| **Sprite Palette** | `0x0C00` - `0x0C3F` | 64 bytes | 1 byte per sprite index (K2GE Color). | `k1ge.cpp:239` |
| **Plane 1 Map** | `0x1000` - `0x17FF` | 2 KB | 32x32 tiles. | `k1ge.cpp:326, 349` |
| | | | Bit 15: H-Flip | `k1ge.cpp:155` |
| | | | Bit 14: V-Flip | `k1ge.cpp:194-197` |
| | | | Bit 13: Mono Palette Select | `k1ge.cpp:185` |
| | | | Bits 12-9: Palette Select (K2GE colour: 16 palettes, `pal_base + ((data & 0x1E00) >> 7)`) | `k1ge.cpp:183` |
| | | | Bits 8-0: Tile Index (`data & 0x1FF`) | `k1ge.cpp:156` |
| **Plane 2 Map** | `0x1800` - `0x1FFF` | 2 KB | 32x32 tiles. Same format as Plane 1. | `k1ge.cpp:332, 343` |
| **Character RAM** | `0x2000` - `0x3FFF` | 8 KB | 512 tiles, 8x8 pixels, 2 bits/pixel. | `k1ge.cpp:156-173` |
| **K2GE Palette RAM** | `0x0200` - `0x03FF` | 1 KB | 256 entries, 12-bit RGB. | `k1ge.cpp:143-150` |

## K2GE video: how a line is drawn

**1. Background & Window**
*   If `line < WBA.V` or `line >= WBA.V + WSI.V`: Fill line with `oow_color()` (`k1ge.cpp:308-313`).
*   Else: Initialize line with `bg_color()` (`k1ge.cpp:317-318`).
*   After drawing: Pixels `0` to `WBA.H-1` and `WBA.H + WSI.H` to `159` overwritten with `oow_color()` (`k1ge.cpp:355-363`).

**2. Draw Order & Priorities**
*   Depends on PF Bit 7 (`0x030`).
*   **PF Bit 7 SET:** Spr 01, Plane 1, Spr 10, Plane 2, Spr 11 (`k1ge.cpp:320-335`).
*   **PF Bit 7 CLEAR:** Spr 01, Plane 2, Spr 10, Plane 1, Spr 11 (`k1ge.cpp:339-352`).
*   Sprite priority check: `(spr_data & 0x1800) == priority << 11`. Values: `0x0800` (01), `0x1000` (10), `0x1800` (11) (`k1ge.cpp:251, 263`).

**3. Sprite Chain**
*   Processed index 0-63 (`k1ge.cpp:254`).
*   **X Pos:** Bit 10 SET: `spr_x += x_offset`. Bit 10 CLEAR: `spr_x = PO.H + x_offset` (`k1ge.cpp:260`).
*   **Y Pos:** Bit 9 SET: `spr_y += y_offset`. Bit 9 CLEAR: `spr_y = PO.V + y_offset` (`k1ge.cpp:261`).
*   **Visibility:** Draw if (`line >= spr_y` OR `spr_y > 0xF8`, a sprite wrapping from the top) AND `line < ((spr_y + 8) & 0xFF)` (`k1ge.cpp:265`).

**4. Tile Data & Flipping**
*   `tile_addr = 0x2000 + (tile_index * 16)` (`k1ge.cpp:156`).
*   V-Flip (Bit 14): `tile_addr += (7 - ((scroll_y + line) & 7)) * 2`. Else: `tile_addr += ((scroll_y + line) & 7) * 2` (`k1ge.cpp:194-197`).
*   H-Flip (Bit 15): Extract LSBs first. Else: Extract MSBs first (`k1ge.cpp:159-173`).

**5. Palette Selection**
*   **K1GE/Compat:** `pcode = mono_color() + pal_base + (BIT(map_data, 13) ? 4 : 0)` (`k1ge.cpp:180-186`).
*   **K2GE Color:** `pcode = pal_base + ((map_data & 0x1E00) >> 7)` (`k1ge.cpp:180-183`).
*   **Sprite K1GE/Compat:** `pcode = mono_color() + (BIT(spr_data, 13) ? 4 : 0)` (`k1ge.cpp:236-242`).
*   **Sprite K2GE Color:** `pcode = (m_vram[0x0C00 + spr_index] & 0x0F) << 2` (`k1ge.cpp:236-242`).

## K2GE video: timing

*   **Screen Height:** 199 lines (`k1ge.h:37`).
*   **Visible Lines:** 0-151 (152 lines): at `y` from 1 to 152 the chip draws line `y - 1` (`k1ge.cpp:416-419`).
*   **Dots per Line:** 480 (`k1ge.cpp:411`).
*   **VBlank Start:** `y >= 152`. Sets bit 6 of `0x010`. Pulses VBlank pin if enabled (`k1ge.cpp:379-386`).
*   **VBlank End:** `y == 0`. Clears bit 6 of `0x010`. Pulses VBlank pin if enabled (`k1ge.cpp:389-400`).
*   **HBlank Trigger:** `y == 198` OR `y < 151`. Pulses HBlank pin high, then low after `time_until_pos(y, 480)` (`k1ge.cpp:403-413`).
*   **Timer:** Fires every line, advancing `y` to `(y + 1) % 199` (`k1ge.cpp:421`).

## Flash cartridge

**Command Sequences**

*   States: `F_READ`, `F_PROG1`, `F_PROG2`, `F_COMMAND`, `F_ID_READ`, `F_AUTO_PROGRAM`, `F_AUTO_CHIP_ERASE`, `F_AUTO_BLOCK_ERASE`, `F_BLOCK_PROTECT` (`ngp.cpp:122-133`).

**Sequence Logic**

1.  `F_READ` -> Write `0xAA` to `0x5555` -> `F_PROG1` (`ngp.cpp:343-344`).
2.  `F_PROG1` -> Write `0x55` to `0x2AAA` -> `F_PROG2` (`ngp.cpp:348-349`).
3.  `F_PROG2` -> Write to `0x5555`:
    *   `0x80`: Chip/Block Erase Prepare -> `F_COMMAND` (`ngp.cpp:449-451`).
    *   `0x90`: ID Read -> Sets ID bytes -> `F_ID_READ` (`ngp.cpp:453-471`).
    *   `0x9A`: Block Protect -> `F_BLOCK_PROTECT` or `F_COMMAND` (`ngp.cpp:472-480`).
    *   `0xA0`: Auto Program -> `F_AUTO_PROGRAM` (`ngp.cpp:481-482`).
    *   `0xF0`: Exit -> `F_READ` (`ngp.cpp:484-486`).
4.  **Erase Execution:**
    *   `F_PROG2` and write `0x30` to address:
        *   If `command[0] == 0x80`: Block Erase (`ngp.cpp:354-356`).
        *   Calculates block addr/size based on `device_id` (`ngp.cpp:362-439`).
        *   Fills block with `0xFF` (`ngp.cpp:440`).
        *   State -> `F_AUTO_BLOCK_ERASE` (`ngp.cpp:361`).
5.  **Program Execution:**
    *   `F_AUTO_PROGRAM`: Writes data to `m_flash_chip[Which].data[offset]` using AND (`data & old_data`) -> `F_READ` (`ngp.cpp:506-509`).
6.  **Exit:**
    *   `F_READ`: Restores original ID bytes from `org_data` to data buffer (`ngp.cpp:522-542`).

**Supported Devices**

*   Manufacturer IDs: `0x98`, `0xEC`, `0xB0` (`ngp.cpp:43`).
*   Device IDs: `0xAB` (4Mbit), `0x2C` (8Mbit), `0x2F` (16Mbit) (`ngp.cpp:44, 766-774`).

**Read Back**

*   Reads from `m_flash_chip[Which].data` (`ngp.cpp:665-666`).
*   ID Read (`0x90`): Writes Manufacturer ID, Device ID, dummy bytes `0x02`, `0x80` to offsets `0x00`, `0x7C000`, `0xFC000`, `0x1FC000` (`ngp.cpp:454-469`).

## BIOS: what the driver gives it

**ROM Regions**

*   NGP: `ngp_bios.ngp` at `0x0000`, size `0x10000` (`ngp.cpp:919-920`).
*   NGPC: `ngpcbios.rom` at `0x0000`, size `0x10000` (`ngp.cpp:925-926`).

**Boot Sequence**

*   If NVRAM loaded (`m_nvram_loaded`), PC set to `0xFF1800` (`ngp.cpp:738-741`).
*   NVRAM Loading: `nvram_read` loads `0x3000` bytes into `m_mainram` and `0x20` bytes into `m_io_reg` (`ngp.cpp:811-814`).
*   Sets current system time to RTC (`ngp.cpp:816-818`).
*   Sets `m_nvram_loaded = true` (`ngp.cpp:820`).

## What Sonic Pocket Adventure uses

*   TMP95C061: timers 0 and 1 (TRUN 0x20, TREG0 0x22, T01MOD 0x24, TRDC 0x29), the watchdog (0x6E, 0x6F), the interrupt levels 0x70, 0x71, 0x73, 0x74, 0x79, 0x7A, and the micro DMA start vectors 0x7C-0x7F, which it only clears (with DMAC0-3, in SYS_PATCH): no micro DMA transfer.
*   NGP I/O: the PSG (0xA0, 0xA1), the DACs (0xA2, 0xA3), the pad (0xB0), the power button (0xB1), the link (0xB2), sound enable (0xB8), the Z80 (0xB9, 0xBA, 0xBC).
*   Video: window, the scroll of both planes and of the sprites, the priority register, colour and mono palettes, the background colour, the sprite table and its palettes, both tile maps, the character RAM.
*   Flash: BIOS call VECT_FLASHWRITE, and its own command sequences (AA/55 at 5555/2AAA, F0 reset) at 3F21xx.
*   BIOS calls (SWI 1): VECT_SHUTDOWN, VECT_CLOCKGEARSET, VECT_RTCGET, VECT_FLASHWRITE, and debug call 0F0h.
*   Frame wait: one place, WaitSomeFrames, called once, from the global object's routine in MainLoop.