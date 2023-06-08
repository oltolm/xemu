/*
 *  Ingenic XBurst Media eXtension Unit (MXU) translation routines.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *  Copyright (c) 2012 Jia Liu & Dongxue Zhang (MIPS ASE DSP support)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Datasheet:
 *
 *   "XBurst® Instruction Set Architecture MIPS eXtension/enhanced Unit
 *   Programming Manual", Ingenic Semiconductor Co, Ltd., revision June 2, 2017
 */

#include "qemu/osdep.h"
#include "translate.h"

/*
 *
 *       AN OVERVIEW OF MXU EXTENSION INSTRUCTION SET
 *       ============================================
 *
 *
 * MXU (full name: MIPS eXtension/enhanced Unit) is a SIMD extension of MIPS32
 * instructions set. It is designed to fit the needs of signal, graphical and
 * video processing applications. MXU instruction set is used in Xburst family
 * of microprocessors by Ingenic.
 *
 * MXU unit contains 17 registers called X0-X16. X0 is always zero, and X16 is
 * the control register.
 *
 *
 *     The notation used in MXU assembler mnemonics
 *     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  Register operands:
 *
 *   XRa, XRb, XRc, XRd - MXU registers
 *   Rb, Rc, Rd, Rs, Rt - general purpose MIPS registers
 *
 *  Non-register operands:
 *
 *   aptn1 - 1-bit accumulate add/subtract pattern
 *   aptn2 - 2-bit accumulate add/subtract pattern
 *   eptn2 - 2-bit execute add/subtract pattern
 *   optn2 - 2-bit operand pattern
 *   optn3 - 3-bit operand pattern
 *   sft4  - 4-bit shift amount
 *   strd2 - 2-bit stride amount
 *
 *  Prefixes:
 *
 *   Level of parallelism:                Operand size:
 *    S - single operation at a time       32 - word
 *    D - two operations in parallel       16 - half word
 *    Q - four operations in parallel       8 - byte
 *
 *  Operations:
 *
 *   ADD   - Add or subtract
 *   ADDC  - Add with carry-in
 *   ACC   - Accumulate
 *   ASUM  - Sum together then accumulate (add or subtract)
 *   ASUMC - Sum together then accumulate (add or subtract) with carry-in
 *   AVG   - Average between 2 operands
 *   ABD   - Absolute difference
 *   ALN   - Align data
 *   AND   - Logical bitwise 'and' operation
 *   CPS   - Copy sign
 *   EXTR  - Extract bits
 *   I2M   - Move from GPR register to MXU register
 *   LDD   - Load data from memory to XRF
 *   LDI   - Load data from memory to XRF (and increase the address base)
 *   LUI   - Load unsigned immediate
 *   MUL   - Multiply
 *   MULU  - Unsigned multiply
 *   MADD  - 64-bit operand add 32x32 product
 *   MSUB  - 64-bit operand subtract 32x32 product
 *   MAC   - Multiply and accumulate (add or subtract)
 *   MAD   - Multiply and add or subtract
 *   MAX   - Maximum between 2 operands
 *   MIN   - Minimum between 2 operands
 *   M2I   - Move from MXU register to GPR register
 *   MOVZ  - Move if zero
 *   MOVN  - Move if non-zero
 *   NOR   - Logical bitwise 'nor' operation
 *   OR    - Logical bitwise 'or' operation
 *   STD   - Store data from XRF to memory
 *   SDI   - Store data from XRF to memory (and increase the address base)
 *   SLT   - Set of less than comparison
 *   SAD   - Sum of absolute differences
 *   SLL   - Logical shift left
 *   SLR   - Logical shift right
 *   SAR   - Arithmetic shift right
 *   SAT   - Saturation
 *   SFL   - Shuffle
 *   SCOP  - Calculate x’s scope (-1, means x<0; 0, means x==0; 1, means x>0)
 *   XOR   - Logical bitwise 'exclusive or' operation
 *
 *  Suffixes:
 *
 *   E - Expand results
 *   F - Fixed point multiplication
 *   L - Low part result
 *   R - Doing rounding
 *   V - Variable instead of immediate
 *   W - Combine above L and V
 *
 *
 *     The list of MXU instructions grouped by functionality
 *     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Load/Store instructions           Multiplication instructions
 * -----------------------           ---------------------------
 *
 *  S32LDD XRa, Rb, s12               S32MADD XRa, XRd, Rs, Rt
 *  S32STD XRa, Rb, s12               S32MADDU XRa, XRd, Rs, Rt
 *  S32LDDV XRa, Rb, rc, strd2        S32MSUB XRa, XRd, Rs, Rt
 *  S32STDV XRa, Rb, rc, strd2        S32MSUBU XRa, XRd, Rs, Rt
 *  S32LDI XRa, Rb, s12               S32MUL XRa, XRd, Rs, Rt
 *  S32SDI XRa, Rb, s12               S32MULU XRa, XRd, Rs, Rt
 *  S32LDIV XRa, Rb, rc, strd2        D16MUL XRa, XRb, XRc, XRd, optn2
 *  S32SDIV XRa, Rb, rc, strd2        D16MULE XRa, XRb, XRc, optn2
 *  S32LDDR XRa, Rb, s12              D16MULF XRa, XRb, XRc, optn2
 *  S32STDR XRa, Rb, s12              D16MAC XRa, XRb, XRc, XRd, aptn2, optn2
 *  S32LDDVR XRa, Rb, rc, strd2       D16MACE XRa, XRb, XRc, XRd, aptn2, optn2
 *  S32STDVR XRa, Rb, rc, strd2       D16MACF XRa, XRb, XRc, XRd, aptn2, optn2
 *  S32LDIR XRa, Rb, s12              D16MADL XRa, XRb, XRc, XRd, aptn2, optn2
 *  S32SDIR XRa, Rb, s12              S16MAD XRa, XRb, XRc, XRd, aptn1, optn2
 *  S32LDIVR XRa, Rb, rc, strd2       Q8MUL XRa, XRb, XRc, XRd
 *  S32SDIVR XRa, Rb, rc, strd2       Q8MULSU XRa, XRb, XRc, XRd
 *  S16LDD XRa, Rb, s10, eptn2        Q8MAC XRa, XRb, XRc, XRd, aptn2
 *  S16STD XRa, Rb, s10, eptn2        Q8MACSU XRa, XRb, XRc, XRd, aptn2
 *  S16LDI XRa, Rb, s10, eptn2        Q8MADL XRa, XRb, XRc, XRd, aptn2
 *  S16SDI XRa, Rb, s10, eptn2
 *  S8LDD XRa, Rb, s8, eptn3
 *  S8STD XRa, Rb, s8, eptn3         Addition and subtraction instructions
 *  S8LDI XRa, Rb, s8, eptn3         -------------------------------------
 *  S8SDI XRa, Rb, s8, eptn3
 *  LXW Rd, Rs, Rt, strd2             D32ADD XRa, XRb, XRc, XRd, eptn2
 *  LXH Rd, Rs, Rt, strd2             D32ADDC XRa, XRb, XRc, XRd
 *  LXHU Rd, Rs, Rt, strd2            D32ACC XRa, XRb, XRc, XRd, eptn2
 *  LXB Rd, Rs, Rt, strd2             D32ACCM XRa, XRb, XRc, XRd, eptn2
 *  LXBU Rd, Rs, Rt, strd2            D32ASUM XRa, XRb, XRc, XRd, eptn2
 *                                    S32CPS XRa, XRb, XRc
 *                                    Q16ADD XRa, XRb, XRc, XRd, eptn2, optn2
 * Comparison instructions            Q16ACC XRa, XRb, XRc, XRd, eptn2
 * -----------------------            Q16ACCM XRa, XRb, XRc, XRd, eptn2
 *                                    D16ASUM XRa, XRb, XRc, XRd, eptn2
 *  S32MAX XRa, XRb, XRc              D16CPS XRa, XRb,
 *  S32MIN XRa, XRb, XRc              D16AVG XRa, XRb, XRc
 *  S32SLT XRa, XRb, XRc              D16AVGR XRa, XRb, XRc
 *  S32MOVZ XRa, XRb, XRc             Q8ADD XRa, XRb, XRc, eptn2
 *  S32MOVN XRa, XRb, XRc             Q8ADDE XRa, XRb, XRc, XRd, eptn2
 *  D16MAX XRa, XRb, XRc              Q8ACCE XRa, XRb, XRc, XRd, eptn2
 *  D16MIN XRa, XRb, XRc              Q8ABD XRa, XRb, XRc
 *  D16SLT XRa, XRb, XRc              Q8SAD XRa, XRb, XRc, XRd
 *  D16MOVZ XRa, XRb, XRc             Q8AVG XRa, XRb, XRc
 *  D16MOVN XRa, XRb, XRc             Q8AVGR XRa, XRb, XRc
 *  Q8MAX XRa, XRb, XRc               D8SUM XRa, XRb, XRc, XRd
 *  Q8MIN XRa, XRb, XRc               D8SUMC XRa, XRb, XRc, XRd
 *  Q8SLT XRa, XRb, XRc
 *  Q8SLTU XRa, XRb, XRc
 *  Q8MOVZ XRa, XRb, XRc             Shift instructions
 *  Q8MOVN XRa, XRb, XRc             ------------------
 *
 *                                    D32SLL XRa, XRb, XRc, XRd, sft4
 * Bitwise instructions               D32SLR XRa, XRb, XRc, XRd, sft4
 * --------------------               D32SAR XRa, XRb, XRc, XRd, sft4
 *                                    D32SARL XRa, XRb, XRc, sft4
 *  S32NOR XRa, XRb, XRc              D32SLLV XRa, XRb, Rb
 *  S32AND XRa, XRb, XRc              D32SLRV XRa, XRb, Rb
 *  S32XOR XRa, XRb, XRc              D32SARV XRa, XRb, Rb
 *  S32OR XRa, XRb, XRc               D32SARW XRa, XRb, XRc, Rb
 *                                    Q16SLL XRa, XRb, XRc, XRd, sft4
 *                                    Q16SLR XRa, XRb, XRc, XRd, sft4
 * Miscellaneous instructions         Q16SAR XRa, XRb, XRc, XRd, sft4
 * -------------------------          Q16SLLV XRa, XRb, Rb
 *                                    Q16SLRV XRa, XRb, Rb
 *  S32SFL XRa, XRb, XRc, XRd, optn2  Q16SARV XRa, XRb, Rb
 *  S32ALN XRa, XRb, XRc, Rb
 *  S32ALNI XRa, XRb, XRc, s3
 *  S32LUI XRa, s8, optn3            Move instructions
 *  S32EXTR XRa, XRb, Rb, bits5      -----------------
 *  S32EXTRV XRa, XRb, Rs, Rt
 *  Q16SCOP XRa, XRb, XRc, XRd        S32M2I XRa, Rb
 *  Q16SAT XRa, XRb, XRc              S32I2M XRa, Rb
 *
 *
 *     The opcode organization of MXU instructions
 *     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * The bits 31..26 of all MXU instructions are equal to 0x1C (also referred
 * as opcode SPECIAL2 in the base MIPS ISA). The organization and meaning of
 * other bits up to the instruction level is as follows:
 *
 *              bits
 *             05..00
 *
 *          ┌─ 000000 ─ OPC_MXU_S32MADD
 *          ├─ 000001 ─ OPC_MXU_S32MADDU
 *          ├─ 000010 ─ <not assigned>   (non-MXU OPC_MUL)
 *          │
 *          │                               20..18
 *          ├─ 000011 ─ OPC_MXU__POOL00 ─┬─ 000 ─ OPC_MXU_S32MAX
 *          │                            ├─ 001 ─ OPC_MXU_S32MIN
 *          │                            ├─ 010 ─ OPC_MXU_D16MAX
 *          │                            ├─ 011 ─ OPC_MXU_D16MIN
 *          │                            ├─ 100 ─ OPC_MXU_Q8MAX
 *          │                            ├─ 101 ─ OPC_MXU_Q8MIN
 *          │                            ├─ 110 ─ OPC_MXU_Q8SLT
 *          │                            └─ 111 ─ OPC_MXU_Q8SLTU
 *          ├─ 000100 ─ OPC_MXU_S32MSUB
 *          ├─ 000101 ─ OPC_MXU_S32MSUBU    20..18
 *          ├─ 000110 ─ OPC_MXU__POOL01 ─┬─ 000 ─ OPC_MXU_S32SLT
 *          │                            ├─ 001 ─ OPC_MXU_D16SLT
 *          │                            ├─ 010 ─ OPC_MXU_D16AVG
 *          │                            ├─ 011 ─ OPC_MXU_D16AVGR
 *          │                            ├─ 100 ─ OPC_MXU_Q8AVG
 *          │                            ├─ 101 ─ OPC_MXU_Q8AVGR
 *          │                            └─ 111 ─ OPC_MXU_Q8ADD
 *          │
 *          │                               20..18
 *          ├─ 000111 ─ OPC_MXU__POOL02 ─┬─ 000 ─ OPC_MXU_S32CPS
 *          │                            ├─ 010 ─ OPC_MXU_D16CPS
 *          │                            ├─ 100 ─ OPC_MXU_Q8ABD
 *          │                            └─ 110 ─ OPC_MXU_Q16SAT
 *          ├─ 001000 ─ OPC_MXU_D16MUL
 *          │                               25..24
 *          ├─ 001001 ─ OPC_MXU__POOL03 ─┬─ 00 ─ OPC_MXU_D16MULF
 *          │                            └─ 01 ─ OPC_MXU_D16MULE
 *          ├─ 001010 ─ OPC_MXU_D16MAC
 *          ├─ 001011 ─ OPC_MXU_D16MACF
 *          ├─ 001100 ─ OPC_MXU_D16MADL
 *          ├─ 001101 ─ OPC_MXU_S16MAD
 *          ├─ 001110 ─ OPC_MXU_Q16ADD
 *          ├─ 001111 ─ OPC_MXU_D16MACE     20 (13..10 don't care)
 *          │                            ┌─ 0 ─ OPC_MXU_S32LDD
 *          ├─ 010000 ─ OPC_MXU__POOL04 ─┴─ 1 ─ OPC_MXU_S32LDDR
 *          │
 *          │                               20 (13..10 don't care)
 *          ├─ 010001 ─ OPC_MXU__POOL05 ─┬─ 0 ─ OPC_MXU_S32STD
 *          │                            └─ 1 ─ OPC_MXU_S32STDR
 *          │
 *          │                               13..10
 *          ├─ 010010 ─ OPC_MXU__POOL06 ─┬─ 0000 ─ OPC_MXU_S32LDDV
 *          │                            └─ 0001 ─ OPC_MXU_S32LDDVR
 *          │
 *          │                               13..10
 *          ├─ 010011 ─ OPC_MXU__POOL07 ─┬─ 0000 ─ OPC_MXU_S32STDV
 *          │                            └─ 0001 ─ OPC_MXU_S32STDVR
 *          │
 *          │                               20 (13..10 don't care)
 *          ├─ 010100 ─ OPC_MXU__POOL08 ─┬─ 0 ─ OPC_MXU_S32LDI
 *          │                            └─ 1 ─ OPC_MXU_S32LDIR
 *          │
 *          │                               20 (13..10 don't care)
 *          ├─ 010101 ─ OPC_MXU__POOL09 ─┬─ 0 ─ OPC_MXU_S32SDI
 *          │                            └─ 1 ─ OPC_MXU_S32SDIR
 *          │
 *          │                               13..10
 *          ├─ 010110 ─ OPC_MXU__POOL10 ─┬─ 0000 ─ OPC_MXU_S32LDIV
 *          │                            └─ 0001 ─ OPC_MXU_S32LDIVR
 *          │
 *          │                               13..10
 *          ├─ 010111 ─ OPC_MXU__POOL11 ─┬─ 0000 ─ OPC_MXU_S32SDIV
 *          │                            └─ 0001 ─ OPC_MXU_S32SDIVR
 *          ├─ 011000 ─ OPC_MXU_D32ADD
 *          │                               23..22
 *   MXU    ├─ 011001 ─ OPC_MXU__POOL12 ─┬─ 00 ─ OPC_MXU_D32ACC
 * opcodes ─┤                            ├─ 01 ─ OPC_MXU_D32ACCM
 *          │                            └─ 10 ─ OPC_MXU_D32ASUM
 *          ├─ 011010 ─ <not assigned>
 *          │                               23..22
 *          ├─ 011011 ─ OPC_MXU__POOL13 ─┬─ 00 ─ OPC_MXU_Q16ACC
 *          │                            ├─ 01 ─ OPC_MXU_Q16ACCM
 *          │                            └─ 10 ─ OPC_MXU_Q16ASUM
 *          │
 *          │                               23..22
 *          ├─ 011100 ─ OPC_MXU__POOL14 ─┬─ 00 ─ OPC_MXU_Q8ADDE
 *          │                            ├─ 01 ─ OPC_MXU_D8SUM
 *          ├─ 011101 ─ OPC_MXU_Q8ACCE   └─ 10 ─ OPC_MXU_D8SUMC
 *          ├─ 011110 ─ <not assigned>
 *          ├─ 011111 ─ <not assigned>
 *          ├─ 100000 ─ <not assigned>   (overlaps with CLZ)
 *          ├─ 100001 ─ <not assigned>   (overlaps with CLO)
 *          ├─ 100010 ─ OPC_MXU_S8LDD
 *          ├─ 100011 ─ OPC_MXU_S8STD       15..14
 *          ├─ 100100 ─ OPC_MXU_S8LDI    ┌─ 00 ─ OPC_MXU_S32MUL
 *          ├─ 100101 ─ OPC_MXU_S8SDI    ├─ 00 ─ OPC_MXU_S32MULU
 *          │                            ├─ 00 ─ OPC_MXU_S32EXTR
 *          ├─ 100110 ─ OPC_MXU__POOL15 ─┴─ 00 ─ OPC_MXU_S32EXTRV
 *          │
 *          │                               20..18
 *          ├─ 100111 ─ OPC_MXU__POOL16 ─┬─ 000 ─ OPC_MXU_D32SARW
 *          │                            ├─ 001 ─ OPC_MXU_S32ALN
 *          │                            ├─ 010 ─ OPC_MXU_S32ALNI
 *          │                            ├─ 011 ─ OPC_MXU_S32LUI
 *          │                            ├─ 100 ─ OPC_MXU_S32NOR
 *          │                            ├─ 101 ─ OPC_MXU_S32AND
 *          │                            ├─ 110 ─ OPC_MXU_S32OR
 *          │                            └─ 111 ─ OPC_MXU_S32XOR
 *          │
 *          │                               8..6
 *          ├─ 101000 ─ OPC_MXU__POOL17 ─┬─ 000 ─ OPC_MXU_LXB
 *          │                            ├─ 001 ─ OPC_MXU_LXH
 *          ├─ 101001 ─ <not assigned>   ├─ 011 ─ OPC_MXU_LXW
 *          ├─ 101010 ─ OPC_MXU_S16LDD   ├─ 100 ─ OPC_MXU_LXBU
 *          ├─ 101011 ─ OPC_MXU_S16STD   └─ 101 ─ OPC_MXU_LXHU
 *          ├─ 101100 ─ OPC_MXU_S16LDI
 *          ├─ 101101 ─ OPC_MXU_S16SDI
 *          ├─ 101110 ─ OPC_MXU_S32M2I
 *          ├─ 101111 ─ OPC_MXU_S32I2M
 *          ├─ 110000 ─ OPC_MXU_D32SLL
 *          ├─ 110001 ─ OPC_MXU_D32SLR      20..18
 *          ├─ 110010 ─ OPC_MXU_D32SARL  ┌─ 000 ─ OPC_MXU_D32SLLV
 *          ├─ 110011 ─ OPC_MXU_D32SAR   ├─ 001 ─ OPC_MXU_D32SLRV
 *          ├─ 110100 ─ OPC_MXU_Q16SLL   ├─ 010 ─ OPC_MXU_D32SARV
 *          ├─ 110101 ─ OPC_MXU_Q16SLR   ├─ 011 ─ OPC_MXU_Q16SLLV
 *          │                            ├─ 100 ─ OPC_MXU_Q16SLRV
 *          ├─ 110110 ─ OPC_MXU__POOL18 ─┴─ 101 ─ OPC_MXU_Q16SARV
 *          │
 *          ├─ 110111 ─ OPC_MXU_Q16SAR
 *          │                               23..22
 *          ├─ 111000 ─ OPC_MXU__POOL19 ─┬─ 00 ─ OPC_MXU_Q8MUL
 *          │                            └─ 01 ─ OPC_MXU_Q8MULSU
 *          │
 *          │                               20..18
 *          ├─ 111001 ─ OPC_MXU__POOL20 ─┬─ 000 ─ OPC_MXU_Q8MOVZ
 *          │                            ├─ 001 ─ OPC_MXU_Q8MOVN
 *          │                            ├─ 010 ─ OPC_MXU_D16MOVZ
 *          │                            ├─ 011 ─ OPC_MXU_D16MOVN
 *          │                            ├─ 100 ─ OPC_MXU_S32MOVZ
 *          │                            └─ 101 ─ OPC_MXU_S32MOVN
 *          │
 *          │                               23..22
 *          ├─ 111010 ─ OPC_MXU__POOL21 ─┬─ 00 ─ OPC_MXU_Q8MAC
 *          │                            └─ 10 ─ OPC_MXU_Q8MACSU
 *          ├─ 111011 ─ OPC_MXU_Q16SCOP
 *          ├─ 111100 ─ OPC_MXU_Q8MADL
 *          ├─ 111101 ─ OPC_MXU_S32SFL
 *          ├─ 111110 ─ OPC_MXU_Q8SAD
 *          └─ 111111 ─ <not assigned>   (overlaps with SDBBP)
 *
 *
 * Compiled after:
 *
 *   "XBurst® Instruction Set Architecture MIPS eXtension/enhanced Unit
 *   Programming Manual", Ingenic Semiconductor Co, Ltd., revision June 2, 2017
 */

enum {
    OPC_MXU_S32MADD  = 0x00,
    OPC_MXU_S32MADDU = 0x01,
    OPC_MXU__POOL00  = 0x03,
    OPC_MXU_S32MSUB  = 0x04,
    OPC_MXU_S32MSUBU = 0x05,
    OPC_MXU__POOL01  = 0x06,
    OPC_MXU__POOL02  = 0x07,
    OPC_MXU_D16MUL   = 0x08,
    OPC_MXU__POOL03  = 0x09,
    OPC_MXU_D16MAC   = 0x0A,
    OPC_MXU_D16MACF  = 0x0B,
    OPC_MXU_D16MADL  = 0x0C,
    OPC_MXU_S16MAD   = 0x0D,
    OPC_MXU_Q16ADD   = 0x0E,
    OPC_MXU_D16MACE  = 0x0F,
    OPC_MXU__POOL04  = 0x10,
    OPC_MXU__POOL05  = 0x11,
    OPC_MXU__POOL06  = 0x12,
    OPC_MXU__POOL07  = 0x13,
    OPC_MXU__POOL08  = 0x14,
    OPC_MXU__POOL09  = 0x15,
    OPC_MXU__POOL10  = 0x16,
    OPC_MXU__POOL11  = 0x17,
    OPC_MXU_D32ADD   = 0x18,
    OPC_MXU_S8LDD    = 0x22,
    OPC_MXU__POOL16  = 0x27,
    OPC_MXU__POOL17  = 0x28,
    OPC_MXU_S32M2I   = 0x2E,
    OPC_MXU_S32I2M   = 0x2F,
    OPC_MXU__POOL19  = 0x38,
};


/*
 * MXU pool 00
 */
enum {
    OPC_MXU_S32MAX   = 0x00,
    OPC_MXU_S32MIN   = 0x01,
    OPC_MXU_D16MAX   = 0x02,
    OPC_MXU_D16MIN   = 0x03,
    OPC_MXU_Q8MAX    = 0x04,
    OPC_MXU_Q8MIN    = 0x05,
    OPC_MXU_Q8SLT    = 0x06,
    OPC_MXU_Q8SLTU   = 0x07,
};

/*
 * MXU pool 01
 */
enum {
    OPC_MXU_S32SLT   = 0x00,
    OPC_MXU_D16SLT   = 0x01,
    OPC_MXU_D16AVG   = 0x02,
    OPC_MXU_D16AVGR  = 0x03,
    OPC_MXU_Q8AVG    = 0x04,
    OPC_MXU_Q8AVGR   = 0x05,
    OPC_MXU_Q8ADD    = 0x07,
};

/*
 * MXU pool 02
 */
enum {
    OPC_MXU_S32CPS   = 0x00,
    OPC_MXU_D16CPS   = 0x02,
    OPC_MXU_Q8ABD    = 0x04,
    OPC_MXU_Q16SAT   = 0x06,
};

/*
 * MXU pool 03
 */
enum {
    OPC_MXU_D16MULF  = 0x00,
    OPC_MXU_D16MULE  = 0x01,
};

/*
 * MXU pool 04 05 06 07 08 09 10 11
 */
enum {
    OPC_MXU_S32LDST  = 0x00,
    OPC_MXU_S32LDSTR = 0x01,
};

/*
 * MXU pool 16
 */
enum {
    OPC_MXU_S32ALNI  = 0x02,
    OPC_MXU_S32NOR   = 0x04,
    OPC_MXU_S32AND   = 0x05,
    OPC_MXU_S32OR    = 0x06,
    OPC_MXU_S32XOR   = 0x07,
};

/*
 * MXU pool 17
 */
enum {
    OPC_MXU_LXB      = 0x00,
    OPC_MXU_LXH      = 0x01,
    OPC_MXU_LXW      = 0x03,
    OPC_MXU_LXBU     = 0x04,
    OPC_MXU_LXHU     = 0x05,
};

/*
 * MXU pool 19
 */
enum {
    OPC_MXU_Q8MUL    = 0x00,
    OPC_MXU_Q8MULSU  = 0x01,
};

/* MXU accumulate add/subtract 1-bit pattern 'aptn1' */
#define MXU_APTN1_A    0
#define MXU_APTN1_S    1

/* MXU accumulate add/subtract 2-bit pattern 'aptn2' */
#define MXU_APTN2_AA    0
#define MXU_APTN2_AS    1
#define MXU_APTN2_SA    2
#define MXU_APTN2_SS    3

/* MXU execute add/subtract 2-bit pattern 'eptn2' */
#define MXU_EPTN2_AA    0
#define MXU_EPTN2_AS    1
#define MXU_EPTN2_SA    2
#define MXU_EPTN2_SS    3

/* MXU operand getting pattern 'optn2' */
#define MXU_OPTN2_PTN0  0
#define MXU_OPTN2_PTN1  1
#define MXU_OPTN2_PTN2  2
#define MXU_OPTN2_PTN3  3
/* alternative naming scheme for 'optn2' */
#define MXU_OPTN2_WW    0
#define MXU_OPTN2_LW    1
#define MXU_OPTN2_HW    2
#define MXU_OPTN2_XW    3

/* MXU operand getting pattern 'optn3' */
#define MXU_OPTN3_PTN0  0
#define MXU_OPTN3_PTN1  1
#define MXU_OPTN3_PTN2  2
#define MXU_OPTN3_PTN3  3
#define MXU_OPTN3_PTN4  4
#define MXU_OPTN3_PTN5  5
#define MXU_OPTN3_PTN6  6
#define MXU_OPTN3_PTN7  7

/* MXU registers */
static TCGv mxu_gpr[NUMBER_OF_MXU_REGISTERS - 1];
static TCGv mxu_CR;

static const char mxuregnames[][4] = {
    "XR1",  "XR2",  "XR3",  "XR4",  "XR5",  "XR6",  "XR7",  "XR8",
    "XR9",  "XR10", "XR11", "XR12", "XR13", "XR14", "XR15", "XCR",
};

void mxu_translate_init(void)
{
    for (unsigned i = 0; i < NUMBER_OF_MXU_REGISTERS - 1; i++) {
        mxu_gpr[i] = tcg_global_mem_new(cpu_env,
                                        offsetof(CPUMIPSState, active_tc.mxu_gpr[i]),
                                        mxuregnames[i]);
    }

    mxu_CR = tcg_global_mem_new(cpu_env,
                                offsetof(CPUMIPSState, active_tc.mxu_cr),
                                mxuregnames[NUMBER_OF_MXU_REGISTERS - 1]);
}

/* MXU General purpose registers moves. */
static inline void gen_load_mxu_gpr(TCGv t, unsigned int reg)
{
    if (reg == 0) {
        tcg_gen_movi_tl(t, 0);
    } else if (reg <= 15) {
        tcg_gen_mov_tl(t, mxu_gpr[reg - 1]);
    }
}

static inline void gen_store_mxu_gpr(TCGv t, unsigned int reg)
{
    if (reg > 0 && reg <= 15) {
        tcg_gen_mov_tl(mxu_gpr[reg - 1], t);
    }
}

/* MXU control register moves. */
static inline void gen_load_mxu_cr(TCGv t)
{
    tcg_gen_mov_tl(t, mxu_CR);
}

static inline void gen_store_mxu_cr(TCGv t)
{
    /* TODO: Add handling of RW rules for MXU_CR. */
    tcg_gen_mov_tl(mxu_CR, t);
}

/*
 * S32I2M XRa, rb - Register move from GRF to XRF
 */
static void gen_mxu_s32i2m(DisasContext *ctx)
{
    TCGv t0;
    uint32_t XRa, Rb;

    t0 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 5);
    Rb = extract32(ctx->opcode, 16, 5);

    gen_load_gpr(t0, Rb);
    if (XRa <= 15) {
        gen_store_mxu_gpr(t0, XRa);
    } else if (XRa == 16) {
        gen_store_mxu_cr(t0);
    }
}

/*
 * S32M2I XRa, rb - Register move from XRF to GRF
 */
static void gen_mxu_s32m2i(DisasContext *ctx)
{
    TCGv t0;
    uint32_t XRa, Rb;

    t0 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 5);
    Rb = extract32(ctx->opcode, 16, 5);

    if (XRa <= 15) {
        gen_load_mxu_gpr(t0, XRa);
    } else if (XRa == 16) {
        gen_load_mxu_cr(t0);
    }

    gen_store_gpr(t0, Rb);
}

/*
 * S8LDD XRa, Rb, s8, optn3 - Load a byte from memory to XRF
 */
static void gen_mxu_s8ldd(DisasContext *ctx)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, s8, optn3;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    s8 = extract32(ctx->opcode, 10, 8);
    optn3 = extract32(ctx->opcode, 18, 3);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    tcg_gen_addi_tl(t0, t0, (int8_t)s8);

    switch (optn3) {
    /* XRa[7:0] = tmp8 */
    case MXU_OPTN3_PTN0:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 0, 8);
        break;
    /* XRa[15:8] = tmp8 */
    case MXU_OPTN3_PTN1:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 8, 8);
        break;
    /* XRa[23:16] = tmp8 */
    case MXU_OPTN3_PTN2:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 16, 8);
        break;
    /* XRa[31:24] = tmp8 */
    case MXU_OPTN3_PTN3:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 24, 8);
        break;
    /* XRa = {8'b0, tmp8, 8'b0, tmp8} */
    case MXU_OPTN3_PTN4:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        tcg_gen_deposit_tl(t0, t1, t1, 16, 16);
        break;
    /* XRa = {tmp8, 8'b0, tmp8, 8'b0} */
    case MXU_OPTN3_PTN5:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        tcg_gen_shli_tl(t1, t1, 8);
        tcg_gen_deposit_tl(t0, t1, t1, 16, 16);
        break;
    /* XRa = {{8{sign of tmp8}}, tmp8, {8{sign of tmp8}}, tmp8} */
    case MXU_OPTN3_PTN6:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_SB);
        tcg_gen_mov_tl(t0, t1);
        tcg_gen_andi_tl(t0, t0, 0xFF00FFFF);
        tcg_gen_shli_tl(t1, t1, 16);
        tcg_gen_or_tl(t0, t0, t1);
        break;
    /* XRa = {tmp8, tmp8, tmp8, tmp8} */
    case MXU_OPTN3_PTN7:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        tcg_gen_deposit_tl(t1, t1, t1, 8, 8);
        tcg_gen_deposit_tl(t0, t1, t1, 16, 16);
        break;
    }

    gen_store_mxu_gpr(t0, XRa);
}

/*
 * D16MUL  XRa, XRb, XRc, XRd, optn2 - Signed 16 bit pattern multiplication
 * D16MULF XRa, XRb, XRc, optn2 - Signed Q15 fraction pattern multiplication
 *   with rounding and packing result
 * D16MULE XRa, XRb, XRc, XRd, optn2 - Signed Q15 fraction pattern
 *   multiplication with rounding
 */
static void gen_mxu_d16mul(DisasContext *ctx, bool fractional,
                           bool packed_result)
{
    TCGv t0, t1, t2, t3;
    uint32_t XRa, XRb, XRc, XRd, optn2;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    optn2 = extract32(ctx->opcode, 22, 2);

    /*
     * TODO: XRd field isn't used for D16MULF
     * There's no knowledge how this field affect
     * instruction decoding/behavior
     */

    gen_load_mxu_gpr(t1, XRb);
    tcg_gen_sextract_tl(t0, t1, 0, 16);
    tcg_gen_sextract_tl(t1, t1, 16, 16);
    gen_load_mxu_gpr(t3, XRc);
    tcg_gen_sextract_tl(t2, t3, 0, 16);
    tcg_gen_sextract_tl(t3, t3, 16, 16);

    switch (optn2) {
    case MXU_OPTN2_WW: /* XRB.H*XRC.H == lop, XRB.L*XRC.L == rop */
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_LW: /* XRB.L*XRC.H == lop, XRB.L*XRC.L == rop */
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_HW: /* XRB.H*XRC.H == lop, XRB.H*XRC.L == rop */
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    case MXU_OPTN2_XW: /* XRB.L*XRC.H == lop, XRB.H*XRC.L == rop */
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    }
    if (fractional) {
        TCGLabel *l_done = gen_new_label();
        TCGv rounding = tcg_temp_new();

        tcg_gen_shli_tl(t3, t3, 1);
        tcg_gen_shli_tl(t2, t2, 1);
        tcg_gen_andi_tl(rounding, mxu_CR, 0x2);
        tcg_gen_brcondi_tl(TCG_COND_EQ, rounding, 0, l_done);
        if (packed_result) {
            TCGLabel *l_apply_bias_l = gen_new_label();
            TCGLabel *l_apply_bias_r = gen_new_label();
            TCGLabel *l_half_done = gen_new_label();
            TCGv bias = tcg_temp_new();

            /*
             * D16MULF supports unbiased rounding aka "bankers rounding",
             * "round to even", "convergent rounding"
             */
            tcg_gen_andi_tl(bias, mxu_CR, 0x4);
            tcg_gen_brcondi_tl(TCG_COND_NE, bias, 0, l_apply_bias_l);
            tcg_gen_andi_tl(t0, t3, 0x1ffff);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0x8000, l_half_done);
            gen_set_label(l_apply_bias_l);
            tcg_gen_addi_tl(t3, t3, 0x8000);
            gen_set_label(l_half_done);
            tcg_gen_brcondi_tl(TCG_COND_NE, bias, 0, l_apply_bias_r);
            tcg_gen_andi_tl(t0, t2, 0x1ffff);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0x8000, l_done);
            gen_set_label(l_apply_bias_r);
            tcg_gen_addi_tl(t2, t2, 0x8000);
        } else {
            /* D16MULE doesn't support unbiased rounding */
            tcg_gen_addi_tl(t3, t3, 0x8000);
            tcg_gen_addi_tl(t2, t2, 0x8000);
        }
        gen_set_label(l_done);
    }
    if (!packed_result) {
        gen_store_mxu_gpr(t3, XRa);
        gen_store_mxu_gpr(t2, XRd);
    } else {
        tcg_gen_andi_tl(t3, t3, 0xffff0000);
        tcg_gen_shri_tl(t2, t2, 16);
        tcg_gen_or_tl(t3, t3, t2);
        gen_store_mxu_gpr(t3, XRa);
    }
}

/*
 * D16MAC XRa, XRb, XRc, XRd, aptn2, optn2
 *   Signed 16 bit pattern multiply and accumulate
 * D16MACF XRa, XRb, XRc, aptn2, optn2
 *   Signed Q15 fraction pattern multiply accumulate and pack
 * D16MACE XRa, XRb, XRc, XRd, aptn2, optn2
 *   Signed Q15 fraction pattern multiply and accumulate
 */
static void gen_mxu_d16mac(DisasContext *ctx, bool fractional,
                           bool packed_result)
{
    TCGv t0, t1, t2, t3;
    uint32_t XRa, XRb, XRc, XRd, optn2, aptn2;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    optn2 = extract32(ctx->opcode, 22, 2);
    aptn2 = extract32(ctx->opcode, 24, 2);

    gen_load_mxu_gpr(t1, XRb);
    tcg_gen_sextract_tl(t0, t1, 0, 16);
    tcg_gen_sextract_tl(t1, t1, 16, 16);

    gen_load_mxu_gpr(t3, XRc);
    tcg_gen_sextract_tl(t2, t3, 0, 16);
    tcg_gen_sextract_tl(t3, t3, 16, 16);

    switch (optn2) {
    case MXU_OPTN2_WW: /* XRB.H*XRC.H == lop, XRB.L*XRC.L == rop */
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_LW: /* XRB.L*XRC.H == lop, XRB.L*XRC.L == rop */
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_HW: /* XRB.H*XRC.H == lop, XRB.H*XRC.L == rop */
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    case MXU_OPTN2_XW: /* XRB.L*XRC.H == lop, XRB.H*XRC.L == rop */
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    }

    if (fractional) {
        tcg_gen_shli_tl(t3, t3, 1);
        tcg_gen_shli_tl(t2, t2, 1);
    }
    gen_load_mxu_gpr(t0, XRa);
    gen_load_mxu_gpr(t1, XRd);

    switch (aptn2) {
    case MXU_APTN2_AA:
        tcg_gen_add_tl(t3, t0, t3);
        tcg_gen_add_tl(t2, t1, t2);
        break;
    case MXU_APTN2_AS:
        tcg_gen_add_tl(t3, t0, t3);
        tcg_gen_sub_tl(t2, t1, t2);
        break;
    case MXU_APTN2_SA:
        tcg_gen_sub_tl(t3, t0, t3);
        tcg_gen_add_tl(t2, t1, t2);
        break;
    case MXU_APTN2_SS:
        tcg_gen_sub_tl(t3, t0, t3);
        tcg_gen_sub_tl(t2, t1, t2);
        break;
    }

    if (fractional) {
        TCGLabel *l_done = gen_new_label();
        TCGv rounding = tcg_temp_new();

        tcg_gen_andi_tl(rounding, mxu_CR, 0x2);
        tcg_gen_brcondi_tl(TCG_COND_EQ, rounding, 0, l_done);
        if (packed_result) {
            TCGLabel *l_apply_bias_l = gen_new_label();
            TCGLabel *l_apply_bias_r = gen_new_label();
            TCGLabel *l_half_done = gen_new_label();
            TCGv bias = tcg_temp_new();

            /*
             * D16MACF supports unbiased rounding aka "bankers rounding",
             * "round to even", "convergent rounding"
             */
            tcg_gen_andi_tl(bias, mxu_CR, 0x4);
            tcg_gen_brcondi_tl(TCG_COND_NE, bias, 0, l_apply_bias_l);
            tcg_gen_andi_tl(t0, t3, 0x1ffff);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0x8000, l_half_done);
            gen_set_label(l_apply_bias_l);
            tcg_gen_addi_tl(t3, t3, 0x8000);
            gen_set_label(l_half_done);
            tcg_gen_brcondi_tl(TCG_COND_NE, bias, 0, l_apply_bias_r);
            tcg_gen_andi_tl(t0, t2, 0x1ffff);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0x8000, l_done);
            gen_set_label(l_apply_bias_r);
            tcg_gen_addi_tl(t2, t2, 0x8000);
        } else {
            /* D16MACE doesn't support unbiased rounding */
            tcg_gen_addi_tl(t3, t3, 0x8000);
            tcg_gen_addi_tl(t2, t2, 0x8000);
        }
        gen_set_label(l_done);
    }

    if (!packed_result) {
        gen_store_mxu_gpr(t3, XRa);
        gen_store_mxu_gpr(t2, XRd);
    } else {
        tcg_gen_andi_tl(t3, t3, 0xffff0000);
        tcg_gen_shri_tl(t2, t2, 16);
        tcg_gen_or_tl(t3, t3, t2);
        gen_store_mxu_gpr(t3, XRa);
    }
}

/*
 * D16MADL XRa, XRb, XRc, XRd, aptn2, optn2 - Double packed
 * unsigned 16 bit pattern multiply and add/subtract.
 */
static void gen_mxu_d16madl(DisasContext *ctx)
{
    TCGv t0, t1, t2, t3;
    uint32_t XRa, XRb, XRc, XRd, optn2, aptn2;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    optn2 = extract32(ctx->opcode, 22, 2);
    aptn2 = extract32(ctx->opcode, 24, 2);

    gen_load_mxu_gpr(t1, XRb);
    tcg_gen_sextract_tl(t0, t1,  0, 16);
    tcg_gen_sextract_tl(t1, t1, 16, 16);

    gen_load_mxu_gpr(t3, XRc);
    tcg_gen_sextract_tl(t2, t3,  0, 16);
    tcg_gen_sextract_tl(t3, t3, 16, 16);

    switch (optn2) {
    case MXU_OPTN2_WW: /* XRB.H*XRC.H == lop, XRB.L*XRC.L == rop */
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_LW: /* XRB.L*XRC.H == lop, XRB.L*XRC.L == rop */
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_HW: /* XRB.H*XRC.H == lop, XRB.H*XRC.L == rop */
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    case MXU_OPTN2_XW: /* XRB.L*XRC.H == lop, XRB.H*XRC.L == rop */
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    }
    tcg_gen_extract_tl(t2, t2, 0, 16);
    tcg_gen_extract_tl(t3, t3, 0, 16);

    gen_load_mxu_gpr(t1, XRa);
    tcg_gen_extract_tl(t0, t1,  0, 16);
    tcg_gen_extract_tl(t1, t1, 16, 16);

    switch (aptn2) {
    case MXU_APTN2_AA:
        tcg_gen_add_tl(t3, t1, t3);
        tcg_gen_add_tl(t2, t0, t2);
        break;
    case MXU_APTN2_AS:
        tcg_gen_add_tl(t3, t1, t3);
        tcg_gen_sub_tl(t2, t0, t2);
        break;
    case MXU_APTN2_SA:
        tcg_gen_sub_tl(t3, t1, t3);
        tcg_gen_add_tl(t2, t0, t2);
        break;
    case MXU_APTN2_SS:
        tcg_gen_sub_tl(t3, t1, t3);
        tcg_gen_sub_tl(t2, t0, t2);
        break;
    }

    tcg_gen_andi_tl(t2, t2, 0xffff);
    tcg_gen_shli_tl(t3, t3, 16);
    tcg_gen_or_tl(mxu_gpr[XRd - 1], t3, t2);
}

/*
 * S16MAD XRa, XRb, XRc, XRd, aptn2, optn2 - Single packed
 * signed 16 bit pattern multiply and 32-bit add/subtract.
 */
static void gen_mxu_s16mad(DisasContext *ctx)
{
    TCGv t0, t1;
    uint32_t XRa, XRb, XRc, XRd, optn2, aptn1, pad;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    optn2 = extract32(ctx->opcode, 22, 2);
    aptn1 = extract32(ctx->opcode, 24, 1);
    pad = extract32(ctx->opcode, 25, 1);

    if (pad) {
        /* FIXME check if it influence the result */
    }

    gen_load_mxu_gpr(t0, XRb);
    gen_load_mxu_gpr(t1, XRc);

    switch (optn2) {
    case MXU_OPTN2_WW: /* XRB.H*XRC.H */
        tcg_gen_sextract_tl(t0, t0, 16, 16);
        tcg_gen_sextract_tl(t1, t1, 16, 16);
        break;
    case MXU_OPTN2_LW: /* XRB.L*XRC.L */
        tcg_gen_sextract_tl(t0, t0,  0, 16);
        tcg_gen_sextract_tl(t1, t1,  0, 16);
        break;
    case MXU_OPTN2_HW: /* XRB.H*XRC.L */
        tcg_gen_sextract_tl(t0, t0, 16, 16);
        tcg_gen_sextract_tl(t1, t1,  0, 16);
        break;
    case MXU_OPTN2_XW: /* XRB.L*XRC.H */
        tcg_gen_sextract_tl(t0, t0,  0, 16);
        tcg_gen_sextract_tl(t1, t1, 16, 16);
        break;
    }
    tcg_gen_mul_tl(t0, t0, t1);

    gen_load_mxu_gpr(t1, XRa);

    switch (aptn1) {
    case MXU_APTN1_A:
        tcg_gen_add_tl(t1, t1, t0);
        break;
    case MXU_APTN1_S:
        tcg_gen_sub_tl(t1, t1, t0);
        break;
    }

    gen_store_mxu_gpr(t1, XRd);
}

/*
 * Q8MUL   XRa, XRb, XRc, XRd - Parallel unsigned 8 bit pattern multiply
 * Q8MULSU XRa, XRb, XRc, XRd - Parallel signed 8 bit pattern multiply
 */
static void gen_mxu_q8mul_q8mulsu(DisasContext *ctx)
{
    TCGv t0, t1, t2, t3, t4, t5, t6, t7;
    uint32_t XRa, XRb, XRc, XRd, sel;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();
    t4 = tcg_temp_new();
    t5 = tcg_temp_new();
    t6 = tcg_temp_new();
    t7 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    sel = extract32(ctx->opcode, 22, 2);

    gen_load_mxu_gpr(t3, XRb);
    gen_load_mxu_gpr(t7, XRc);

    if (sel == 0x2) {
        /* Q8MULSU */
        tcg_gen_ext8s_tl(t0, t3);
        tcg_gen_shri_tl(t3, t3, 8);
        tcg_gen_ext8s_tl(t1, t3);
        tcg_gen_shri_tl(t3, t3, 8);
        tcg_gen_ext8s_tl(t2, t3);
        tcg_gen_shri_tl(t3, t3, 8);
        tcg_gen_ext8s_tl(t3, t3);
    } else {
        /* Q8MUL */
        tcg_gen_ext8u_tl(t0, t3);
        tcg_gen_shri_tl(t3, t3, 8);
        tcg_gen_ext8u_tl(t1, t3);
        tcg_gen_shri_tl(t3, t3, 8);
        tcg_gen_ext8u_tl(t2, t3);
        tcg_gen_shri_tl(t3, t3, 8);
        tcg_gen_ext8u_tl(t3, t3);
    }

    tcg_gen_ext8u_tl(t4, t7);
    tcg_gen_shri_tl(t7, t7, 8);
    tcg_gen_ext8u_tl(t5, t7);
    tcg_gen_shri_tl(t7, t7, 8);
    tcg_gen_ext8u_tl(t6, t7);
    tcg_gen_shri_tl(t7, t7, 8);
    tcg_gen_ext8u_tl(t7, t7);

    tcg_gen_mul_tl(t0, t0, t4);
    tcg_gen_mul_tl(t1, t1, t5);
    tcg_gen_mul_tl(t2, t2, t6);
    tcg_gen_mul_tl(t3, t3, t7);

    tcg_gen_andi_tl(t0, t0, 0xFFFF);
    tcg_gen_andi_tl(t1, t1, 0xFFFF);
    tcg_gen_andi_tl(t2, t2, 0xFFFF);
    tcg_gen_andi_tl(t3, t3, 0xFFFF);

    tcg_gen_shli_tl(t1, t1, 16);
    tcg_gen_shli_tl(t3, t3, 16);

    tcg_gen_or_tl(t0, t0, t1);
    tcg_gen_or_tl(t1, t2, t3);

    gen_store_mxu_gpr(t0, XRd);
    gen_store_mxu_gpr(t1, XRa);
}

/*
 * S32LDD  XRa, Rb, S12 - Load a word from memory to XRF
 * S32LDDR XRa, Rb, S12 - Load a word from memory to XRF
 *   in reversed byte seq.
 * S32LDI  XRa, Rb, S12 - Load a word from memory to XRF,
 *   post modify base address GPR.
 * S32LDIR XRa, Rb, S12 - Load a word from memory to XRF,
 *   post modify base address GPR and load in reversed byte seq.
 */
static void gen_mxu_s32ldxx(DisasContext *ctx, bool reversed, bool postinc)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, s12;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    s12 = sextract32(ctx->opcode, 10, 10);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    tcg_gen_movi_tl(t1, s12 * 4);
    tcg_gen_add_tl(t0, t0, t1);

    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx,
                       (MO_TESL ^ (reversed ? MO_BSWAP : 0)) |
                        ctx->default_tcg_memop_mask);
    gen_store_mxu_gpr(t1, XRa);

    if (postinc) {
        gen_store_gpr(t0, Rb);
    }
}

/*
 * S32STD  XRa, Rb, S12 - Store a word from XRF to memory
 * S32STDR XRa, Rb, S12 - Store a word from XRF to memory
 *   in reversed byte seq.
 * S32SDI  XRa, Rb, S12 - Store a word from XRF to memory,
 *   post modify base address GPR.
 * S32SDIR XRa, Rb, S12 - Store a word from XRF to memory,
 *   post modify base address GPR and store in reversed byte seq.
 */
static void gen_mxu_s32stxx(DisasContext *ctx, bool reversed, bool postinc)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, s12;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    s12 = sextract32(ctx->opcode, 10, 10);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    tcg_gen_movi_tl(t1, s12 * 4);
    tcg_gen_add_tl(t0, t0, t1);

    gen_load_mxu_gpr(t1, XRa);
    tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx,
                       (MO_TESL ^ (reversed ? MO_BSWAP : 0)) |
                        ctx->default_tcg_memop_mask);

    if (postinc) {
        gen_store_gpr(t0, Rb);
    }
}

/*
 * S32LDDV  XRa, Rb, Rc, STRD2 - Load a word from memory to XRF
 * S32LDDVR XRa, Rb, Rc, STRD2 - Load a word from memory to XRF
 *   in reversed byte seq.
 * S32LDIV  XRa, Rb, Rc, STRD2 - Load a word from memory to XRF,
 *   post modify base address GPR.
 * S32LDIVR XRa, Rb, Rc, STRD2 - Load a word from memory to XRF,
 *   post modify base address GPR and load in reversed byte seq.
 */
static void gen_mxu_s32ldxvx(DisasContext *ctx, bool reversed,
                             bool postinc, uint32_t strd2)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, Rc;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    Rc = extract32(ctx->opcode, 16, 5);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    gen_load_gpr(t1, Rc);
    tcg_gen_shli_tl(t1, t1, strd2);
    tcg_gen_add_tl(t0, t0, t1);

    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx,
                       (MO_TESL ^ (reversed ? MO_BSWAP : 0)) |
                        ctx->default_tcg_memop_mask);
    gen_store_mxu_gpr(t1, XRa);

    if (postinc) {
        gen_store_gpr(t0, Rb);
    }
}

/*
 * LXW  Ra, Rb, Rc, STRD2 - Load a word from memory to GPR
 * LXB  Ra, Rb, Rc, STRD2 - Load a byte from memory to GPR,
 *   sign extending to GPR size.
 * LXH  Ra, Rb, Rc, STRD2 - Load a byte from memory to GPR,
 *   sign extending to GPR size.
 * LXBU Ra, Rb, Rc, STRD2 - Load a halfword from memory to GPR,
 *   zero extending to GPR size.
 * LXHU Ra, Rb, Rc, STRD2 - Load a halfword from memory to GPR,
 *   zero extending to GPR size.
 */
static void gen_mxu_lxx(DisasContext *ctx, uint32_t strd2, MemOp mop)
{
    TCGv t0, t1;
    uint32_t Ra, Rb, Rc;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    Ra = extract32(ctx->opcode, 11, 5);
    Rc = extract32(ctx->opcode, 16, 5);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    gen_load_gpr(t1, Rc);
    tcg_gen_shli_tl(t1, t1, strd2);
    tcg_gen_add_tl(t0, t0, t1);

    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, mop | ctx->default_tcg_memop_mask);
    gen_store_gpr(t1, Ra);
}

/*
 * S32STDV  XRa, Rb, Rc, STRD2 - Load a word from memory to XRF
 * S32STDVR XRa, Rb, Rc, STRD2 - Load a word from memory to XRF
 *   in reversed byte seq.
 * S32SDIV  XRa, Rb, Rc, STRD2 - Load a word from memory to XRF,
 *   post modify base address GPR.
 * S32SDIVR XRa, Rb, Rc, STRD2 - Load a word from memory to XRF,
 *   post modify base address GPR and store in reversed byte seq.
 */
static void gen_mxu_s32stxvx(DisasContext *ctx, bool reversed,
                             bool postinc, uint32_t strd2)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, Rc;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    Rc = extract32(ctx->opcode, 16, 5);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    gen_load_gpr(t1, Rc);
    tcg_gen_shli_tl(t1, t1, strd2);
    tcg_gen_add_tl(t0, t0, t1);

    gen_load_mxu_gpr(t1, XRa);
    tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx,
                       (MO_TESL ^ (reversed ? MO_BSWAP : 0)) |
                        ctx->default_tcg_memop_mask);

    if (postinc) {
        gen_store_gpr(t0, Rb);
    }
}

/*
 *                 MXU instruction category: logic
 *                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *               S32NOR    S32AND    S32OR    S32XOR
 */

/*
 *  S32NOR XRa, XRb, XRc
 *    Update XRa with the result of logical bitwise 'nor' operation
 *    applied to the content of XRb and XRc.
 */
static void gen_mxu_S32NOR(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to all 1s */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0xFFFFFFFF);
    } else if (unlikely(XRb == 0)) {
        /* XRb zero register -> just set destination to the negation of XRc */
        tcg_gen_not_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
    } else if (unlikely(XRc == 0)) {
        /* XRa zero register -> just set destination to the negation of XRb */
        tcg_gen_not_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same -> just set destination to the negation of XRb */
        tcg_gen_not_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        tcg_gen_nor_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], mxu_gpr[XRc - 1]);
    }
}

/*
 *  S32AND XRa, XRb, XRc
 *    Update XRa with the result of logical bitwise 'and' operation
 *    applied to the content of XRb and XRc.
 */
static void gen_mxu_S32AND(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) || (XRc == 0))) {
        /* one of operands zero register -> just set destination to all 0s */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same -> just set destination to one of them */
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        tcg_gen_and_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], mxu_gpr[XRc - 1]);
    }
}

/*
 *  S32OR XRa, XRb, XRc
 *    Update XRa with the result of logical bitwise 'or' operation
 *    applied to the content of XRb and XRc.
 */
static void gen_mxu_S32OR(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to all 0s */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == 0)) {
        /* XRb zero register -> just set destination to the content of XRc */
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
    } else if (unlikely(XRc == 0)) {
        /* XRc zero register -> just set destination to the content of XRb */
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same -> just set destination to one of them */
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        tcg_gen_or_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], mxu_gpr[XRc - 1]);
    }
}

/*
 *  S32XOR XRa, XRb, XRc
 *    Update XRa with the result of logical bitwise 'xor' operation
 *    applied to the content of XRb and XRc.
 */
static void gen_mxu_S32XOR(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to all 0s */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == 0)) {
        /* XRb zero register -> just set destination to the content of XRc */
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
    } else if (unlikely(XRc == 0)) {
        /* XRc zero register -> just set destination to the content of XRb */
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same -> just set destination to all 0s */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else {
        /* the most general case */
        tcg_gen_xor_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], mxu_gpr[XRc - 1]);
    }
}


/*
 *                   MXU instruction category max/min/avg
 *                   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *                     S32MAX     D16MAX     Q8MAX
 *                     S32MIN     D16MIN     Q8MIN
 *                     S32SLT     D16SLT     Q8SLT
 *                                           Q8SLTU
 *                                D16AVG     Q8AVG
 *                                D16AVGR    Q8AVGR
 */

/*
 *  S32MAX XRa, XRb, XRc
 *    Update XRa with the maximum of signed 32-bit integers contained
 *    in XRb and XRc.
 *
 *  S32MIN XRa, XRb, XRc
 *    Update XRa with the minimum of signed 32-bit integers contained
 *    in XRb and XRc.
 */
static void gen_mxu_S32MAX_S32MIN(DisasContext *ctx)
{
    uint32_t pad, opc, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    opc = extract32(ctx->opcode, 18, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely((XRb == 0) || (XRc == 0))) {
        /* exactly one operand is zero register - find which one is not...*/
        uint32_t XRx = XRb ? XRb : XRc;
        /* ...and do max/min operation with one operand 0 */
        if (opc == OPC_MXU_S32MAX) {
            tcg_gen_smax_i32(mxu_gpr[XRa - 1], mxu_gpr[XRx - 1], 0);
        } else {
            tcg_gen_smin_i32(mxu_gpr[XRa - 1], mxu_gpr[XRx - 1], 0);
        }
    } else if (unlikely(XRb == XRc)) {
        /* both operands same -> just set destination to one of them */
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        if (opc == OPC_MXU_S32MAX) {
            tcg_gen_smax_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1],
                                               mxu_gpr[XRc - 1]);
        } else {
            tcg_gen_smin_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1],
                                               mxu_gpr[XRc - 1]);
        }
    }
}

/*
 *  D16MAX
 *    Update XRa with the 16-bit-wise maximums of signed integers
 *    contained in XRb and XRc.
 *
 *  D16MIN
 *    Update XRa with the 16-bit-wise minimums of signed integers
 *    contained in XRb and XRc.
 */
static void gen_mxu_D16MAX_D16MIN(DisasContext *ctx)
{
    uint32_t pad, opc, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    opc = extract32(ctx->opcode, 18, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely((XRb == 0) || (XRc == 0))) {
        /* exactly one operand is zero register - find which one is not...*/
        uint32_t XRx = XRb ? XRb : XRc;
        /* ...and do half-word-wise max/min with one operand 0 */
        TCGv_i32 t0 = tcg_temp_new();
        TCGv_i32 t1 = tcg_constant_i32(0);
        TCGv_i32 t2 = tcg_temp_new();

        /* the left half-word first */
        tcg_gen_andi_i32(t0, mxu_gpr[XRx - 1], 0xFFFF0000);
        if (opc == OPC_MXU_D16MAX) {
            tcg_gen_smax_i32(t2, t0, t1);
        } else {
            tcg_gen_smin_i32(t2, t0, t1);
        }

        /* the right half-word */
        tcg_gen_andi_i32(t0, mxu_gpr[XRx - 1], 0x0000FFFF);
        /* move half-words to the leftmost position */
        tcg_gen_shli_i32(t0, t0, 16);
        /* t0 will be max/min of t0 and t1 */
        if (opc == OPC_MXU_D16MAX) {
            tcg_gen_smax_i32(t0, t0, t1);
        } else {
            tcg_gen_smin_i32(t0, t0, t1);
        }
        /* return resulting half-words to its original position */
        tcg_gen_shri_i32(t0, t0, 16);
        /* finally update the destination */
        tcg_gen_or_i32(mxu_gpr[XRa - 1], t2, t0);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same -> just set destination to one of them */
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        TCGv_i32 t0 = tcg_temp_new();
        TCGv_i32 t1 = tcg_temp_new();
        TCGv_i32 t2 = tcg_temp_new();

        /* the left half-word first */
        tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0xFFFF0000);
        tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFFFF0000);
        if (opc == OPC_MXU_D16MAX) {
            tcg_gen_smax_i32(t2, t0, t1);
        } else {
            tcg_gen_smin_i32(t2, t0, t1);
        }

        /* the right half-word */
        tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0x0000FFFF);
        tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0x0000FFFF);
        /* move half-words to the leftmost position */
        tcg_gen_shli_i32(t0, t0, 16);
        tcg_gen_shli_i32(t1, t1, 16);
        /* t0 will be max/min of t0 and t1 */
        if (opc == OPC_MXU_D16MAX) {
            tcg_gen_smax_i32(t0, t0, t1);
        } else {
            tcg_gen_smin_i32(t0, t0, t1);
        }
        /* return resulting half-words to its original position */
        tcg_gen_shri_i32(t0, t0, 16);
        /* finally update the destination */
        tcg_gen_or_i32(mxu_gpr[XRa - 1], t2, t0);
    }
}

/*
 *  Q8MAX
 *    Update XRa with the 8-bit-wise maximums of signed integers
 *    contained in XRb and XRc.
 *
 *  Q8MIN
 *    Update XRa with the 8-bit-wise minimums of signed integers
 *    contained in XRb and XRc.
 */
static void gen_mxu_Q8MAX_Q8MIN(DisasContext *ctx)
{
    uint32_t pad, opc, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    opc = extract32(ctx->opcode, 18, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely((XRb == 0) || (XRc == 0))) {
        /* exactly one operand is zero register - make it be the first...*/
        uint32_t XRx = XRb ? XRb : XRc;
        /* ...and do byte-wise max/min with one operand 0 */
        TCGv_i32 t0 = tcg_temp_new();
        TCGv_i32 t1 = tcg_constant_i32(0);
        TCGv_i32 t2 = tcg_temp_new();
        int32_t i;

        /* the leftmost byte (byte 3) first */
        tcg_gen_andi_i32(t0, mxu_gpr[XRx - 1], 0xFF000000);
        if (opc == OPC_MXU_Q8MAX) {
            tcg_gen_smax_i32(t2, t0, t1);
        } else {
            tcg_gen_smin_i32(t2, t0, t1);
        }

        /* bytes 2, 1, 0 */
        for (i = 2; i >= 0; i--) {
            /* extract the byte */
            tcg_gen_andi_i32(t0, mxu_gpr[XRx - 1], 0xFF << (8 * i));
            /* move the byte to the leftmost position */
            tcg_gen_shli_i32(t0, t0, 8 * (3 - i));
            /* t0 will be max/min of t0 and t1 */
            if (opc == OPC_MXU_Q8MAX) {
                tcg_gen_smax_i32(t0, t0, t1);
            } else {
                tcg_gen_smin_i32(t0, t0, t1);
            }
            /* return resulting byte to its original position */
            tcg_gen_shri_i32(t0, t0, 8 * (3 - i));
            /* finally update the destination */
            tcg_gen_or_i32(t2, t2, t0);
        }
        gen_store_mxu_gpr(t2, XRa);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same -> just set destination to one of them */
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        TCGv_i32 t0 = tcg_temp_new();
        TCGv_i32 t1 = tcg_temp_new();
        TCGv_i32 t2 = tcg_temp_new();
        int32_t i;

        /* the leftmost bytes (bytes 3) first */
        tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0xFF000000);
        tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFF000000);
        if (opc == OPC_MXU_Q8MAX) {
            tcg_gen_smax_i32(t2, t0, t1);
        } else {
            tcg_gen_smin_i32(t2, t0, t1);
        }

        /* bytes 2, 1, 0 */
        for (i = 2; i >= 0; i--) {
            /* extract corresponding bytes */
            tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0xFF << (8 * i));
            tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFF << (8 * i));
            /* move the bytes to the leftmost position */
            tcg_gen_shli_i32(t0, t0, 8 * (3 - i));
            tcg_gen_shli_i32(t1, t1, 8 * (3 - i));
            /* t0 will be max/min of t0 and t1 */
            if (opc == OPC_MXU_Q8MAX) {
                tcg_gen_smax_i32(t0, t0, t1);
            } else {
                tcg_gen_smin_i32(t0, t0, t1);
            }
            /* return resulting byte to its original position */
            tcg_gen_shri_i32(t0, t0, 8 * (3 - i));
            /* finally update the destination */
            tcg_gen_or_i32(t2, t2, t0);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

/*
 *  Q8SLT
 *    Update XRa with the signed "set less than" comparison of XRb and XRc
 *    on per-byte basis.
 *    a.k.a. XRa[0..3] = XRb[0..3] < XRc[0..3] ? 1 : 0;
 *
 *  Q8SLTU
 *    Update XRa with the unsigned "set less than" comparison of XRb and XRc
 *    on per-byte basis.
 *    a.k.a. XRa[0..3] = XRb[0..3] < XRc[0..3] ? 1 : 0;
 */
static void gen_mxu_q8slt(DisasContext *ctx, bool sltu)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same registers -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        /* the most general case */
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_movi_tl(t2, 0);

        for (int i = 0; i < 4; i++) {
            if (sltu) {
                tcg_gen_extract_tl(t0, t3, 8 * i, 8);
                tcg_gen_extract_tl(t1, t4, 8 * i, 8);
            } else {
                tcg_gen_sextract_tl(t0, t3, 8 * i, 8);
                tcg_gen_sextract_tl(t1, t4, 8 * i, 8);
            }
            tcg_gen_setcond_tl(TCG_COND_LT, t0, t0, t1);
            tcg_gen_deposit_tl(t2, t2, t0, 8 * i, 8);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

/*
 *  S32SLT
 *    Update XRa with the signed "set less than" comparison of XRb and XRc.
 *    a.k.a. XRa = XRb < XRc ? 1 : 0;
 */
static void gen_mxu_S32SLT(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same registers -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        /* the most general case */
        tcg_gen_setcond_tl(TCG_COND_LT, mxu_gpr[XRa - 1],
                           mxu_gpr[XRb - 1], mxu_gpr[XRc - 1]);
    }
}

/*
 *  D16SLT
 *    Update XRa with the signed "set less than" comparison of XRb and XRc
 *    on per-word basis.
 *    a.k.a. XRa[0..1] = XRb[0..1] < XRc[0..1] ? 1 : 0;
 */
static void gen_mxu_D16SLT(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same registers -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        /* the most general case */
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_sextract_tl(t0, t3, 16, 16);
        tcg_gen_sextract_tl(t1, t4, 16, 16);
        tcg_gen_setcond_tl(TCG_COND_LT, t0, t0, t1);
        tcg_gen_shli_tl(t2, t0, 16);
        tcg_gen_sextract_tl(t0, t3,  0, 16);
        tcg_gen_sextract_tl(t1, t4,  0, 16);
        tcg_gen_setcond_tl(TCG_COND_LT, t0, t0, t1);
        tcg_gen_or_tl(mxu_gpr[XRa - 1], t2, t0);
    }
}

/*
 *  D16AVG
 *    Update XRa with the signed average of XRb and XRc
 *    on per-word basis, rounding down.
 *    a.k.a. XRa[0..1] = (XRb[0..1] + XRc[0..1]) >> 1;
 *
 *  D16AVGR
 *    Update XRa with the signed average of XRb and XRc
 *    on per-word basis, math rounding 4/5.
 *    a.k.a. XRa[0..1] = (XRb[0..1] + XRc[0..1] + 1) >> 1;
 */
static void gen_mxu_d16avg(DisasContext *ctx, bool round45)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same registers -> just set destination to same */
        tcg_gen_mov_tl(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_sextract_tl(t0, t3, 16, 16);
        tcg_gen_sextract_tl(t1, t4, 16, 16);
        tcg_gen_add_tl(t0, t0, t1);
        if (round45) {
            tcg_gen_addi_tl(t0, t0, 1);
        }
        tcg_gen_shli_tl(t2, t0, 15);
        tcg_gen_andi_tl(t2, t2, 0xffff0000);
        tcg_gen_sextract_tl(t0, t3,  0, 16);
        tcg_gen_sextract_tl(t1, t4,  0, 16);
        tcg_gen_add_tl(t0, t0, t1);
        if (round45) {
            tcg_gen_addi_tl(t0, t0, 1);
        }
        tcg_gen_shri_tl(t0, t0, 1);
        tcg_gen_deposit_tl(t2, t2, t0, 0, 16);
        gen_store_mxu_gpr(t2, XRa);
    }
}

/*
 *  Q8AVG
 *    Update XRa with the signed average of XRb and XRc
 *    on per-byte basis, rounding down.
 *    a.k.a. XRa[0..3] = (XRb[0..3] + XRc[0..3]) >> 1;
 *
 *  Q8AVGR
 *    Update XRa with the signed average of XRb and XRc
 *    on per-word basis, math rounding 4/5.
 *    a.k.a. XRa[0..3] = (XRb[0..3] + XRc[0..3] + 1) >> 1;
 */
static void gen_mxu_q8avg(DisasContext *ctx, bool round45)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        /* both operands same registers -> just set destination to same */
        tcg_gen_mov_tl(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_movi_tl(t2, 0);

        for (int i = 0; i < 4; i++) {
            tcg_gen_extract_tl(t0, t3, 8 * i, 8);
            tcg_gen_extract_tl(t1, t4, 8 * i, 8);
            tcg_gen_add_tl(t0, t0, t1);
            if (round45) {
                tcg_gen_addi_tl(t0, t0, 1);
            }
            tcg_gen_shri_tl(t0, t0, 1);
            tcg_gen_deposit_tl(t2, t2, t0, 8 * i, 8);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}


/*
 *      MXU instruction category: Addition and subtraction
 *      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *              S32CPS      D16CPS
 *                                       Q8ADD
 */

/*
 *  S32CPS
 *    Update XRa if XRc < 0 by value of 0 - XRb
 *    else XRa = XRb
 */
static void gen_mxu_S32CPS(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely(XRb == 0)) {
        /* XRc make no sense 0 - 0 = 0 -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRc == 0)) {
        /* condition always false -> just move XRb to XRa */
        tcg_gen_mov_tl(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        TCGv t0 = tcg_temp_new();
        TCGLabel *l_not_less = gen_new_label();
        TCGLabel *l_done = gen_new_label();

        tcg_gen_brcondi_tl(TCG_COND_GE, mxu_gpr[XRc - 1], 0, l_not_less);
        tcg_gen_neg_tl(t0, mxu_gpr[XRb - 1]);
        tcg_gen_br(l_done);
        gen_set_label(l_not_less);
        gen_load_mxu_gpr(t0, XRb);
        gen_set_label(l_done);
        gen_store_mxu_gpr(t0, XRa);
    }
}

/*
 *  D16CPS
 *    Update XRa[0..1] if XRc[0..1] < 0 by value of 0 - XRb[0..1]
 *    else XRa[0..1] = XRb[0..1]
 */
static void gen_mxu_D16CPS(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely(XRb == 0)) {
        /* XRc make no sense 0 - 0 = 0 -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRc == 0)) {
        /* condition always false -> just move XRb to XRa */
        tcg_gen_mov_tl(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        /* the most general case */
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGLabel *l_done_hi = gen_new_label();
        TCGLabel *l_not_less_lo = gen_new_label();
        TCGLabel *l_done_lo = gen_new_label();

        tcg_gen_sextract_tl(t0, mxu_gpr[XRc - 1], 16, 16);
        tcg_gen_sextract_tl(t1, mxu_gpr[XRb - 1], 16, 16);
        tcg_gen_brcondi_tl(TCG_COND_GE, t0, 0, l_done_hi);
        tcg_gen_subfi_tl(t1, 0, t1);

        gen_set_label(l_done_hi);
        tcg_gen_shli_i32(t1, t1, 16);

        tcg_gen_sextract_tl(t0, mxu_gpr[XRc - 1],  0, 16);
        tcg_gen_brcondi_tl(TCG_COND_GE, t0, 0, l_not_less_lo);
        tcg_gen_sextract_tl(t0, mxu_gpr[XRb - 1],  0, 16);
        tcg_gen_subfi_tl(t0, 0, t0);
        tcg_gen_br(l_done_lo);

        gen_set_label(l_not_less_lo);
        tcg_gen_extract_tl(t0, mxu_gpr[XRb - 1],  0, 16);

        gen_set_label(l_done_lo);
        tcg_gen_deposit_tl(mxu_gpr[XRa - 1], t1, t0, 0, 16);
    }
}

/*
 *  Q8ABD XRa, XRb, XRc
 *  Gets absolute difference for quadruple of 8-bit
 *  packed in XRb to another one in XRc,
 *  put the result in XRa.
 *  a.k.a. XRa[0..3] = abs(XRb[0..3] - XRc[0..3]);
 */
static void gen_mxu_Q8ABD(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        /* the most general case */
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_movi_tl(t2, 0);

        for (int i = 0; i < 4; i++) {
            tcg_gen_extract_tl(t0, t3, 8 * i, 8);
            tcg_gen_extract_tl(t1, t4, 8 * i, 8);

            tcg_gen_sub_tl(t0, t0, t1);
            tcg_gen_abs_tl(t0, t0);

            tcg_gen_deposit_tl(t2, t2, t0, 8 * i, 8);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

/*
 *  Q8ADD XRa, XRb, XRc, ptn2
 *  Add/subtract quadruple of 8-bit packed in XRb
 *  to another one in XRc, put the result in XRa.
 */
static void gen_mxu_Q8ADD(DisasContext *ctx)
{
    uint32_t aptn2, pad, XRc, XRb, XRa;

    aptn2 = extract32(ctx->opcode, 24, 2);
    pad   = extract32(ctx->opcode, 21, 3);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to zero */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else {
        /* the most general case */
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);

        for (int i = 0; i < 4; i++) {
            tcg_gen_andi_tl(t0, t3, 0xff);
            tcg_gen_andi_tl(t1, t4, 0xff);

            if (i < 2) {
                if (aptn2 & 0x01) {
                    tcg_gen_sub_tl(t0, t0, t1);
                } else {
                    tcg_gen_add_tl(t0, t0, t1);
                }
            } else {
                if (aptn2 & 0x02) {
                    tcg_gen_sub_tl(t0, t0, t1);
                } else {
                    tcg_gen_add_tl(t0, t0, t1);
                }
            }
            if (i < 3) {
                tcg_gen_shri_tl(t3, t3, 8);
                tcg_gen_shri_tl(t4, t4, 8);
            }
            if (i > 0) {
                tcg_gen_deposit_tl(t2, t2, t0, 8 * i, 8);
            } else {
                tcg_gen_andi_tl(t0, t0, 0xff);
                tcg_gen_mov_tl(t2, t0);
            }
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

/*
 * Q16ADD XRa, XRb, XRc, XRd, aptn2, optn2 - Quad packed
 * 16-bit pattern addition.
 */
static void gen_mxu_q16add(DisasContext *ctx)
{
    uint32_t aptn2, optn2, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    optn2 = extract32(ctx->opcode, 22, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_new();
    TCGv t5 = tcg_temp_new();

    gen_load_mxu_gpr(t1, XRb);
    tcg_gen_extract_tl(t0, t1,  0, 16);
    tcg_gen_extract_tl(t1, t1, 16, 16);

    gen_load_mxu_gpr(t3, XRc);
    tcg_gen_extract_tl(t2, t3,  0, 16);
    tcg_gen_extract_tl(t3, t3, 16, 16);

    switch (optn2) {
    case MXU_OPTN2_WW: /* XRB.H+XRC.H == lop, XRB.L+XRC.L == rop */
        tcg_gen_mov_tl(t4, t1);
        tcg_gen_mov_tl(t5, t0);
        break;
    case MXU_OPTN2_LW: /* XRB.L+XRC.H == lop, XRB.L+XRC.L == rop */
        tcg_gen_mov_tl(t4, t0);
        tcg_gen_mov_tl(t5, t0);
        break;
    case MXU_OPTN2_HW: /* XRB.H+XRC.H == lop, XRB.H+XRC.L == rop */
        tcg_gen_mov_tl(t4, t1);
        tcg_gen_mov_tl(t5, t1);
        break;
    case MXU_OPTN2_XW: /* XRB.L+XRC.H == lop, XRB.H+XRC.L == rop */
        tcg_gen_mov_tl(t4, t0);
        tcg_gen_mov_tl(t5, t1);
        break;
    }

    switch (aptn2) {
    case MXU_APTN2_AA: /* lop +, rop + */
        tcg_gen_add_tl(t0, t4, t3);
        tcg_gen_add_tl(t1, t5, t2);
        tcg_gen_add_tl(t4, t4, t3);
        tcg_gen_add_tl(t5, t5, t2);
        break;
    case MXU_APTN2_AS: /* lop +, rop + */
        tcg_gen_sub_tl(t0, t4, t3);
        tcg_gen_sub_tl(t1, t5, t2);
        tcg_gen_add_tl(t4, t4, t3);
        tcg_gen_add_tl(t5, t5, t2);
        break;
    case MXU_APTN2_SA: /* lop +, rop + */
        tcg_gen_add_tl(t0, t4, t3);
        tcg_gen_add_tl(t1, t5, t2);
        tcg_gen_sub_tl(t4, t4, t3);
        tcg_gen_sub_tl(t5, t5, t2);
        break;
    case MXU_APTN2_SS: /* lop +, rop + */
        tcg_gen_sub_tl(t0, t4, t3);
        tcg_gen_sub_tl(t1, t5, t2);
        tcg_gen_sub_tl(t4, t4, t3);
        tcg_gen_sub_tl(t5, t5, t2);
        break;
    }

    tcg_gen_shli_tl(t0, t0, 16);
    tcg_gen_extract_tl(t1, t1, 0, 16);
    tcg_gen_shli_tl(t4, t4, 16);
    tcg_gen_extract_tl(t5, t5, 0, 16);

    tcg_gen_or_tl(mxu_gpr[XRa - 1], t4, t5);
    tcg_gen_or_tl(mxu_gpr[XRd - 1], t0, t1);
}

/*
 * D32ADD XRa, XRb, XRc, XRd, aptn2 - Double
 * 32 bit pattern addition/subtraction.
 */
static void gen_mxu_d32add(DisasContext *ctx)
{
    uint32_t aptn2, pad, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    pad   = extract32(ctx->opcode, 22, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv carry = tcg_temp_new();
    TCGv cr = tcg_temp_new();

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0 && XRd == 0)) {
        /* destinations are zero register -> do nothing */
    } else {
        /* common case */
        gen_load_mxu_gpr(t0, XRb);
        gen_load_mxu_gpr(t1, XRc);
        gen_load_mxu_cr(cr);
        if (XRa != 0) {
            if (aptn2 & 2) {
                tcg_gen_sub_i32(t2, t0, t1);
                tcg_gen_setcond_tl(TCG_COND_GTU, carry, t0, t1);
            } else {
                tcg_gen_add_i32(t2, t0, t1);
                tcg_gen_setcond_tl(TCG_COND_GTU, carry, t0, t2);
            }
            tcg_gen_andi_tl(cr, cr, 0x7fffffff);
            tcg_gen_shli_tl(carry, carry, 31);
            tcg_gen_or_tl(cr, cr, carry);
            gen_store_mxu_gpr(t2, XRa);
        }
        if (XRd != 0) {
            if (aptn2 & 1) {
                tcg_gen_sub_i32(t2, t0, t1);
                tcg_gen_setcond_tl(TCG_COND_GTU, carry, t0, t1);
            } else {
                tcg_gen_add_i32(t2, t0, t1);
                tcg_gen_setcond_tl(TCG_COND_GTU, carry, t0, t2);
            }
            tcg_gen_andi_tl(cr, cr, 0xbfffffff);
            tcg_gen_shli_tl(carry, carry, 30);
            tcg_gen_or_tl(cr, cr, carry);
            gen_store_mxu_gpr(t2, XRd);
        }
        gen_store_mxu_cr(cr);
    }
}

/*
 *                 MXU instruction category: Miscellaneous
 *                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *                       Q16SAT
 */

/*
 *  Q16SAT XRa, XRb, XRc
 *  Packs four 16-bit signed integers in XRb and XRc to
 *  four saturated unsigned 8-bit into XRa.
 *
 */
static void gen_mxu_Q16SAT(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else {
        /* the most general case */
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();

        tcg_gen_movi_tl(t2, 0);
        if (XRb != 0) {
            TCGLabel *l_less_hi = gen_new_label();
            TCGLabel *l_less_lo = gen_new_label();
            TCGLabel *l_lo = gen_new_label();
            TCGLabel *l_greater_hi = gen_new_label();
            TCGLabel *l_greater_lo = gen_new_label();
            TCGLabel *l_done = gen_new_label();

            tcg_gen_sari_tl(t0, mxu_gpr[XRb - 1], 16);
            tcg_gen_brcondi_tl(TCG_COND_LT, t0, 0, l_less_hi);
            tcg_gen_brcondi_tl(TCG_COND_GT, t0, 255, l_greater_hi);
            tcg_gen_br(l_lo);
            gen_set_label(l_less_hi);
            tcg_gen_movi_tl(t0, 0);
            tcg_gen_br(l_lo);
            gen_set_label(l_greater_hi);
            tcg_gen_movi_tl(t0, 255);

            gen_set_label(l_lo);
            tcg_gen_shli_tl(t1, mxu_gpr[XRb - 1], 16);
            tcg_gen_sari_tl(t1, t1, 16);
            tcg_gen_brcondi_tl(TCG_COND_LT, t1, 0, l_less_lo);
            tcg_gen_brcondi_tl(TCG_COND_GT, t1, 255, l_greater_lo);
            tcg_gen_br(l_done);
            gen_set_label(l_less_lo);
            tcg_gen_movi_tl(t1, 0);
            tcg_gen_br(l_done);
            gen_set_label(l_greater_lo);
            tcg_gen_movi_tl(t1, 255);

            gen_set_label(l_done);
            tcg_gen_shli_tl(t2, t0, 24);
            tcg_gen_shli_tl(t1, t1, 16);
            tcg_gen_or_tl(t2, t2, t1);
        }

        if (XRc != 0) {
            TCGLabel *l_less_hi = gen_new_label();
            TCGLabel *l_less_lo = gen_new_label();
            TCGLabel *l_lo = gen_new_label();
            TCGLabel *l_greater_hi = gen_new_label();
            TCGLabel *l_greater_lo = gen_new_label();
            TCGLabel *l_done = gen_new_label();

            tcg_gen_sari_tl(t0, mxu_gpr[XRc - 1], 16);
            tcg_gen_brcondi_tl(TCG_COND_LT, t0, 0, l_less_hi);
            tcg_gen_brcondi_tl(TCG_COND_GT, t0, 255, l_greater_hi);
            tcg_gen_br(l_lo);
            gen_set_label(l_less_hi);
            tcg_gen_movi_tl(t0, 0);
            tcg_gen_br(l_lo);
            gen_set_label(l_greater_hi);
            tcg_gen_movi_tl(t0, 255);

            gen_set_label(l_lo);
            tcg_gen_shli_tl(t1, mxu_gpr[XRc - 1], 16);
            tcg_gen_sari_tl(t1, t1, 16);
            tcg_gen_brcondi_tl(TCG_COND_LT, t1, 0, l_less_lo);
            tcg_gen_brcondi_tl(TCG_COND_GT, t1, 255, l_greater_lo);
            tcg_gen_br(l_done);
            gen_set_label(l_less_lo);
            tcg_gen_movi_tl(t1, 0);
            tcg_gen_br(l_done);
            gen_set_label(l_greater_lo);
            tcg_gen_movi_tl(t1, 255);

            gen_set_label(l_done);
            tcg_gen_shli_tl(t0, t0, 8);
            tcg_gen_or_tl(t2, t2, t0);
            tcg_gen_or_tl(t2, t2, t1);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}


/*
 *                 MXU instruction category: align
 *                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *                       S32ALN     S32ALNI
 */

/*
 *  S32ALNI XRc, XRb, XRa, optn3
 *    Arrange bytes from XRb and XRc according to one of five sets of
 *    rules determined by optn3, and place the result in XRa.
 */
static void gen_mxu_S32ALNI(DisasContext *ctx)
{
    uint32_t optn3, pad, XRc, XRb, XRa;

    optn3 = extract32(ctx->opcode,  23, 3);
    pad   = extract32(ctx->opcode,  21, 2);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
        /* opcode padding incorrect -> do nothing */
    } else if (unlikely(XRa == 0)) {
        /* destination is zero register -> do nothing */
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        /* both operands zero registers -> just set destination to all 0s */
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == 0)) {
        /* XRb zero register -> just appropriatelly shift XRc into XRa */
        switch (optn3) {
        case MXU_OPTN3_PTN0:
            tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
            break;
        case MXU_OPTN3_PTN1:
        case MXU_OPTN3_PTN2:
        case MXU_OPTN3_PTN3:
            tcg_gen_shri_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1],
                             8 * (4 - optn3));
            break;
        case MXU_OPTN3_PTN4:
            tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
            break;
        }
    } else if (unlikely(XRc == 0)) {
        /* XRc zero register -> just appropriatelly shift XRb into XRa */
        switch (optn3) {
        case MXU_OPTN3_PTN0:
            tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
            break;
        case MXU_OPTN3_PTN1:
        case MXU_OPTN3_PTN2:
        case MXU_OPTN3_PTN3:
            tcg_gen_shri_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], 8 * optn3);
            break;
        case MXU_OPTN3_PTN4:
            tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
            break;
        }
    } else if (unlikely(XRb == XRc)) {
        /* both operands same -> just rotation or moving from any of them */
        switch (optn3) {
        case MXU_OPTN3_PTN0:
        case MXU_OPTN3_PTN4:
            tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
            break;
        case MXU_OPTN3_PTN1:
        case MXU_OPTN3_PTN2:
        case MXU_OPTN3_PTN3:
            tcg_gen_rotli_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], 8 * optn3);
            break;
        }
    } else {
        /* the most general case */
        switch (optn3) {
        case MXU_OPTN3_PTN0:
            {
                /*                                         */
                /*         XRb                XRc          */
                /*  +---------------+                      */
                /*  | A   B   C   D |    E   F   G   H     */
                /*  +-------+-------+                      */
                /*          |                              */
                /*         XRa                             */
                /*                                         */

                tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
            }
            break;
        case MXU_OPTN3_PTN1:
            {
                /*                                         */
                /*         XRb                 XRc         */
                /*      +-------------------+              */
                /*    A | B   C   D       E | F   G   H    */
                /*      +---------+---------+              */
                /*                |                        */
                /*               XRa                       */
                /*                                         */

                TCGv_i32 t0 = tcg_temp_new();
                TCGv_i32 t1 = tcg_temp_new();

                tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0x00FFFFFF);
                tcg_gen_shli_i32(t0, t0, 8);

                tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFF000000);
                tcg_gen_shri_i32(t1, t1, 24);

                tcg_gen_or_i32(mxu_gpr[XRa - 1], t0, t1);
            }
            break;
        case MXU_OPTN3_PTN2:
            {
                /*                                         */
                /*         XRb                 XRc         */
                /*          +-------------------+          */
                /*    A   B | C   D       E   F | G   H    */
                /*          +---------+---------+          */
                /*                    |                    */
                /*                   XRa                   */
                /*                                         */

                TCGv_i32 t0 = tcg_temp_new();
                TCGv_i32 t1 = tcg_temp_new();

                tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0x0000FFFF);
                tcg_gen_shli_i32(t0, t0, 16);

                tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFFFF0000);
                tcg_gen_shri_i32(t1, t1, 16);

                tcg_gen_or_i32(mxu_gpr[XRa - 1], t0, t1);
            }
            break;
        case MXU_OPTN3_PTN3:
            {
                /*                                         */
                /*         XRb                 XRc         */
                /*              +-------------------+      */
                /*    A   B   C | D       E   F   G | H    */
                /*              +---------+---------+      */
                /*                        |                */
                /*                       XRa               */
                /*                                         */

                TCGv_i32 t0 = tcg_temp_new();
                TCGv_i32 t1 = tcg_temp_new();

                tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0x000000FF);
                tcg_gen_shli_i32(t0, t0, 24);

                tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFFFFFF00);
                tcg_gen_shri_i32(t1, t1, 8);

                tcg_gen_or_i32(mxu_gpr[XRa - 1], t0, t1);
            }
            break;
        case MXU_OPTN3_PTN4:
            {
                /*                                         */
                /*         XRb                 XRc         */
                /*                     +---------------+   */
                /*    A   B   C   D    | E   F   G   H |   */
                /*                     +-------+-------+   */
                /*                             |           */
                /*                            XRa          */
                /*                                         */

                tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
            }
            break;
        }
    }
}

/*
 *  S32MADD XRa, XRd, rb, rc
 *    32 to 64 bit signed multiply with subsequent add
 *    result stored in {XRa, XRd} pair, stain HI/LO.
 *  S32MADDU XRa, XRd, rb, rc
 *    32 to 64 bit unsigned multiply with subsequent add
 *    result stored in {XRa, XRd} pair, stain HI/LO.
 *  S32MSUB XRa, XRd, rb, rc
 *    32 to 64 bit signed multiply with subsequent subtract
 *    result stored in {XRa, XRd} pair, stain HI/LO.
 *  S32MSUBU XRa, XRd, rb, rc
 *    32 to 64 bit unsigned multiply with subsequent subtract
 *    result stored in {XRa, XRd} pair, stain HI/LO.
 */
static void gen_mxu_s32madd_sub(DisasContext *ctx, bool sub, bool uns)
{
    uint32_t XRa, XRd, Rb, Rc;

    XRa  = extract32(ctx->opcode,  6, 4);
    XRd  = extract32(ctx->opcode, 10, 4);
    Rb   = extract32(ctx->opcode, 16, 5);
    Rc   = extract32(ctx->opcode, 21, 5);

    if (unlikely(Rb == 0 || Rc == 0)) {
        /* do nothing because x + 0 * y => x */
    } else if (unlikely(XRa == 0 && XRd == 0)) {
        /* do nothing because result just dropped */
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv_i64 t2 = tcg_temp_new_i64();
        TCGv_i64 t3 = tcg_temp_new_i64();

        gen_load_gpr(t0, Rb);
        gen_load_gpr(t1, Rc);

        if (uns) {
            tcg_gen_extu_tl_i64(t2, t0);
            tcg_gen_extu_tl_i64(t3, t1);
        } else {
            tcg_gen_ext_tl_i64(t2, t0);
            tcg_gen_ext_tl_i64(t3, t1);
        }
        tcg_gen_mul_i64(t2, t2, t3);

        gen_load_mxu_gpr(t0, XRa);
        gen_load_mxu_gpr(t1, XRd);

        tcg_gen_concat_tl_i64(t3, t1, t0);
        if (sub) {
            tcg_gen_sub_i64(t3, t3, t2);
        } else {
            tcg_gen_add_i64(t3, t3, t2);
        }
        gen_move_low32(t1, t3);
        gen_move_high32(t0, t3);

        tcg_gen_mov_tl(cpu_HI[0], t0);
        tcg_gen_mov_tl(cpu_LO[0], t1);

        gen_store_mxu_gpr(t1, XRd);
        gen_store_mxu_gpr(t0, XRa);
    }
}

/*
 * Decoding engine for MXU
 * =======================
 */

static void decode_opc_mxu__pool00(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_S32MAX:
    case OPC_MXU_S32MIN:
        gen_mxu_S32MAX_S32MIN(ctx);
        break;
    case OPC_MXU_D16MAX:
    case OPC_MXU_D16MIN:
        gen_mxu_D16MAX_D16MIN(ctx);
        break;
    case OPC_MXU_Q8MAX:
    case OPC_MXU_Q8MIN:
        gen_mxu_Q8MAX_Q8MIN(ctx);
        break;
    case OPC_MXU_Q8SLT:
        gen_mxu_q8slt(ctx, false);
        break;
    case OPC_MXU_Q8SLTU:
        gen_mxu_q8slt(ctx, true);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static bool decode_opc_mxu_s32madd_sub(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 0, 6);
    uint32_t pad  = extract32(ctx->opcode, 14, 2);

    if (pad != 2) {
        /* MIPS32R1 MADD/MADDU/MSUB/MSUBU are on pad == 0 */
        return false;
    }

    switch (opcode) {
    case OPC_MXU_S32MADD:
        gen_mxu_s32madd_sub(ctx, false, false);
        break;
    case OPC_MXU_S32MADDU:
        gen_mxu_s32madd_sub(ctx, false, true);
        break;
    case OPC_MXU_S32MSUB:
        gen_mxu_s32madd_sub(ctx, true, false);
        break;
    case OPC_MXU_S32MSUBU:
        gen_mxu_s32madd_sub(ctx, true, true);
        break;
    default:
        return false;
    }
    return true;
}

static void decode_opc_mxu__pool01(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_S32SLT:
        gen_mxu_S32SLT(ctx);
        break;
    case OPC_MXU_D16SLT:
        gen_mxu_D16SLT(ctx);
        break;
    case OPC_MXU_D16AVG:
        gen_mxu_d16avg(ctx, false);
        break;
    case OPC_MXU_D16AVGR:
        gen_mxu_d16avg(ctx, true);
        break;
    case OPC_MXU_Q8AVG:
        gen_mxu_q8avg(ctx, false);
        break;
    case OPC_MXU_Q8AVGR:
        gen_mxu_q8avg(ctx, true);
        break;
    case OPC_MXU_Q8ADD:
        gen_mxu_Q8ADD(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool02(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_S32CPS:
        gen_mxu_S32CPS(ctx);
        break;
    case OPC_MXU_D16CPS:
        gen_mxu_D16CPS(ctx);
        break;
    case OPC_MXU_Q8ABD:
        gen_mxu_Q8ABD(ctx);
        break;
    case OPC_MXU_Q16SAT:
        gen_mxu_Q16SAT(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool03(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 24, 2);

    switch (opcode) {
    case OPC_MXU_D16MULF:
        gen_mxu_d16mul(ctx, true, true);
        break;
    case OPC_MXU_D16MULE:
        gen_mxu_d16mul(ctx, true, false);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool04(DisasContext *ctx)
{
    uint32_t reversed = extract32(ctx->opcode, 20, 1);
    uint32_t opcode = extract32(ctx->opcode, 10, 4);

    /* Don't care about opcode bits as their meaning is unknown yet */
    switch (opcode) {
    default:
        gen_mxu_s32ldxx(ctx, reversed, false);
        break;
    }
}

static void decode_opc_mxu__pool05(DisasContext *ctx)
{
    uint32_t reversed = extract32(ctx->opcode, 20, 1);
    uint32_t opcode = extract32(ctx->opcode, 10, 4);

    /* Don't care about opcode bits as their meaning is unknown yet */
    switch (opcode) {
    default:
        gen_mxu_s32stxx(ctx, reversed, false);
        break;
    }
}

static void decode_opc_mxu__pool06(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 10, 4);
    uint32_t strd2  = extract32(ctx->opcode, 14, 2);

    switch (opcode) {
    case OPC_MXU_S32LDST:
    case OPC_MXU_S32LDSTR:
        if (strd2 <= 2) {
            gen_mxu_s32ldxvx(ctx, opcode, false, strd2);
            break;
        }
        /* fallthrough */
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool07(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 10, 4);
    uint32_t strd2  = extract32(ctx->opcode, 14, 2);

    switch (opcode) {
    case OPC_MXU_S32LDST:
    case OPC_MXU_S32LDSTR:
        if (strd2 <= 2) {
            gen_mxu_s32stxvx(ctx, opcode, false, strd2);
            break;
        }
        /* fallthrough */
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool08(DisasContext *ctx)
{
    uint32_t reversed = extract32(ctx->opcode, 20, 1);
    uint32_t opcode = extract32(ctx->opcode, 10, 4);

    /* Don't care about opcode bits as their meaning is unknown yet */
    switch (opcode) {
    default:
        gen_mxu_s32ldxx(ctx, reversed, true);
        break;
    }
}

static void decode_opc_mxu__pool09(DisasContext *ctx)
{
    uint32_t reversed = extract32(ctx->opcode, 20, 1);
    uint32_t opcode = extract32(ctx->opcode, 10, 4);

    /* Don't care about opcode bits as their meaning is unknown yet */
    switch (opcode) {
    default:
        gen_mxu_s32stxx(ctx, reversed, true);
        break;
    }
}

static void decode_opc_mxu__pool10(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 10, 4);
    uint32_t strd2  = extract32(ctx->opcode, 14, 2);

    switch (opcode) {
    case OPC_MXU_S32LDST:
    case OPC_MXU_S32LDSTR:
        if (strd2 <= 2) {
            gen_mxu_s32ldxvx(ctx, opcode, true, strd2);
            break;
        }
        /* fallthrough */
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool11(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 10, 4);
    uint32_t strd2  = extract32(ctx->opcode, 14, 2);

    switch (opcode) {
    case OPC_MXU_S32LDST:
    case OPC_MXU_S32LDSTR:
        if (strd2 <= 2) {
            gen_mxu_s32stxvx(ctx, opcode, true, strd2);
            break;
        }
        /* fallthrough */
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool16(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_S32ALNI:
        gen_mxu_S32ALNI(ctx);
        break;
    case OPC_MXU_S32NOR:
        gen_mxu_S32NOR(ctx);
        break;
    case OPC_MXU_S32AND:
        gen_mxu_S32AND(ctx);
        break;
    case OPC_MXU_S32OR:
        gen_mxu_S32OR(ctx);
        break;
    case OPC_MXU_S32XOR:
        gen_mxu_S32XOR(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool17(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 6, 3);
    uint32_t strd2  = extract32(ctx->opcode, 9, 2);

    if (strd2 > 2) {
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        return;
    }

    switch (opcode) {
    case OPC_MXU_LXW:
          gen_mxu_lxx(ctx, strd2, MO_TE | MO_UL);
          break;
    case OPC_MXU_LXB:
          gen_mxu_lxx(ctx, strd2, MO_TE | MO_SB);
          break;
    case OPC_MXU_LXH:
          gen_mxu_lxx(ctx, strd2, MO_TE | MO_SW);
          break;
    case OPC_MXU_LXBU:
          gen_mxu_lxx(ctx, strd2, MO_TE | MO_UB);
          break;
    case OPC_MXU_LXHU:
          gen_mxu_lxx(ctx, strd2, MO_TE | MO_UW);
          break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool19(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 22, 2);

    switch (opcode) {
    case OPC_MXU_Q8MUL:
    case OPC_MXU_Q8MULSU:
        gen_mxu_q8mul_q8mulsu(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

bool decode_ase_mxu(DisasContext *ctx, uint32_t insn)
{
    uint32_t opcode = extract32(insn, 0, 6);

    if (opcode == OPC_MXU_S32M2I) {
        gen_mxu_s32m2i(ctx);
        return true;
    }

    if (opcode == OPC_MXU_S32I2M) {
        gen_mxu_s32i2m(ctx);
        return true;
    }

    {
        TCGv t_mxu_cr = tcg_temp_new();
        TCGLabel *l_exit = gen_new_label();

        gen_load_mxu_cr(t_mxu_cr);
        tcg_gen_andi_tl(t_mxu_cr, t_mxu_cr, MXU_CR_MXU_EN);
        tcg_gen_brcondi_tl(TCG_COND_NE, t_mxu_cr, MXU_CR_MXU_EN, l_exit);

        switch (opcode) {
        case OPC_MXU_S32MADD:
        case OPC_MXU_S32MADDU:
        case OPC_MXU_S32MSUB:
        case OPC_MXU_S32MSUBU:
            return decode_opc_mxu_s32madd_sub(ctx);
        case OPC_MXU__POOL00:
            decode_opc_mxu__pool00(ctx);
            break;
        case OPC_MXU_D16MUL:
            gen_mxu_d16mul(ctx, false, false);
            break;
        case OPC_MXU_D16MAC:
            gen_mxu_d16mac(ctx, false, false);
            break;
        case OPC_MXU_D16MACF:
            gen_mxu_d16mac(ctx, true, true);
            break;
        case OPC_MXU_D16MADL:
            gen_mxu_d16madl(ctx);
            break;
        case OPC_MXU_S16MAD:
            gen_mxu_s16mad(ctx);
            break;
        case OPC_MXU_Q16ADD:
            gen_mxu_q16add(ctx);
            break;
        case OPC_MXU_D16MACE:
            gen_mxu_d16mac(ctx, true, false);
            break;
        case OPC_MXU__POOL01:
            decode_opc_mxu__pool01(ctx);
            break;
        case OPC_MXU__POOL02:
            decode_opc_mxu__pool02(ctx);
            break;
        case OPC_MXU__POOL03:
            decode_opc_mxu__pool03(ctx);
            break;
        case OPC_MXU__POOL04:
            decode_opc_mxu__pool04(ctx);
            break;
        case OPC_MXU__POOL05:
            decode_opc_mxu__pool05(ctx);
            break;
        case OPC_MXU__POOL06:
            decode_opc_mxu__pool06(ctx);
            break;
        case OPC_MXU__POOL07:
            decode_opc_mxu__pool07(ctx);
            break;
        case OPC_MXU__POOL08:
            decode_opc_mxu__pool08(ctx);
            break;
        case OPC_MXU__POOL09:
            decode_opc_mxu__pool09(ctx);
            break;
        case OPC_MXU__POOL10:
            decode_opc_mxu__pool10(ctx);
            break;
        case OPC_MXU__POOL11:
            decode_opc_mxu__pool11(ctx);
            break;
        case OPC_MXU_D32ADD:
            gen_mxu_d32add(ctx);
            break;
        case OPC_MXU_S8LDD:
            gen_mxu_s8ldd(ctx);
            break;
        case OPC_MXU__POOL16:
            decode_opc_mxu__pool16(ctx);
            break;
        case OPC_MXU__POOL17:
            decode_opc_mxu__pool17(ctx);
            break;
        case OPC_MXU__POOL19:
            decode_opc_mxu__pool19(ctx);
            break;
        default:
            return false;
        }

        gen_set_label(l_exit);
    }

    return true;
}
