# SPDX-License-Identifier: Apache-2.0
#
# QPU shader: vec3_transform_perspective.
#
# Each invocation processes NUM_ITERS batches of 16 vertices in SIMD.
# Inputs are SoA: 16 x's, 16 y's, 16 z's contiguous in memory; outputs
# are 16 cx, 16 cy, 16 cz, 16 cw. The same 4x4 matrix is applied to
# every vertex (it's the per-frame model-view-projection matrix in
# wipeout's hot path).
#
# clip = M * vec4(p.x, p.y, p.z, 1.0) (column-major matrix indexing
# matches wipeout's scalar vec3_transform_perspective in types.c).
#
# Uniforms (per QPU):
#   0  qpu_num        — used to pick a private VPM row so 12 parallel
#                        QPUs don't trample each other
#   1  input_base     — bus addr of this QPU's input block
#   2  output_base    — bus addr of this QPU's output block
#   3  num_iters      — how many 16-vertex batches to process
#   4..19  m[0..15]   — column-major MVP matrix
#
# Assemble with:
#   vc4asm -V -C vec3_xform_code.c -h vec3_xform_code.h \
#          -i vec3_xform_code vec3_xform.qasm
#
.include <vc4.qinc>

# ---------- register allocation -----------------------------------
.set qpu_num,     ra0
.set input_base,  ra1
.set output_base, ra2
.set num_iters,   ra3
.set base_row,    ra4

# Matrix in rb (separate file from working values in ra → no read conflicts).
.set m0,  rb0
.set m1,  rb1
.set m2,  rb2
.set m3,  rb3
.set m4,  rb4
.set m5,  rb5
.set m6,  rb6
.set m7,  rb7
.set m8,  rb8
.set m9,  rb9
.set m10, rb10
.set m11, rb11
.set m12, rb12
.set m13, rb13
.set m14, rb14
.set m15, rb15

# Working vec16's (per-lane x/y/z and 4 outputs)
.set x,  ra5
.set y,  ra6
.set z,  ra7
.set cx, ra8
.set cy, ra9
.set cz, ra10
.set cw, ra11

# ---------- uniform load -----------------------------------------
mov qpu_num,    unif
mov input_base, unif
mov output_base, unif
mov num_iters,  unif

# Matrix loads -- one per line; we can't dual-issue two writes to the
# same regfile (m0-m15 are all in rb).
mov m0,  unif
mov m1,  unif
mov m2,  unif
mov m3,  unif
mov m4,  unif
mov m5,  unif
mov m6,  unif
mov m7,  unif
mov m8,  unif
mov m9,  unif
mov m10, unif
mov m11, unif
mov m12, unif
mov m13, unif
mov m14, unif
mov m15, unif

mov base_row, qpu_num

# Small-immediate field is 5 bits, can't hold 64. Stash it in a regfile
# slot to use as the address advance for vec16 (= 64 byte) strides.
ldi rb16, 64

# ---------- helpers ----------------------------------------------

# DMA-read one vec16 (16 floats = 64 bytes) from memory address
# `addr_reg` into VPM row `base_row`, then load that row into reg.
.macro mem_to_reg, dst_reg, addr_reg
    # vdr_setup_0: read 1 row of 16 horizontal 32-bit words
    mov r2, vdr_setup_0(1, 16, 1, vdr_h32(1, 0, 0))
    # row select lives in bits 20-23 of vr_setup -> shift base_row by 4
    shl r3, base_row, 4
    add vr_setup, r2, r3
    mov vr_addr, addr_reg
    mov -, vr_wait
    # set up VPM read at row base_row, horizontal 32-bit
    mov r2, vpm_setup(1, 1, h32(0))
    add vr_setup, r2, base_row
    mov dst_reg, vpm
.endm

# Write reg → VPM row base_row → DMA to memory address addr_reg.
.macro reg_to_mem, src_reg, addr_reg
    mov r2, vpm_setup(1, 1, h32(0))
    add vw_setup, r2, base_row
    mov vpm, src_reg
    # vdw_setup_0: write 1 row of 16 horizontal 32-bit words
    mov r2, vdw_setup_0(1, 16, dma_h32(0, 0))
    # DMA row select lives in bits 23-29 of vw_setup -> shift by 7
    shl r3, base_row, 7
    add vw_setup, r2, r3
    mov vw_addr, addr_reg
    mov -, vw_wait
.endm

# 4-component dot product into dst, using r0/r1 as scratch.
# dst = mA*x + mB*y + mC*z + mD
.macro dot4, dst, mA, mB, mC, mD
    fmul r0, mA, x
    fmul r1, mB, y
    fadd r0, r0, r1
    fmul r1, mC, z
    fadd r0, r0, r1
    fadd dst, r0, mD
.endm

# ---------- main loop --------------------------------------------
:loop
    mem_to_reg x, input_base
    add input_base, input_base, rb16

    mem_to_reg y, input_base
    add input_base, input_base, rb16

    mem_to_reg z, input_base
    add input_base, input_base, rb16

    # Row 0: clip_x = m0*x + m4*y + m8*z + m12
    dot4 cx, m0, m4, m8,  m12
    # Row 1: clip_y = m1*x + m5*y + m9*z + m13
    dot4 cy, m1, m5, m9,  m13
    # Row 2: clip_z = m2*x + m6*y + m10*z + m14
    dot4 cz, m2, m6, m10, m14
    # Row 3: clip_w = m3*x + m7*y + m11*z + m15
    dot4 cw, m3, m7, m11, m15

    reg_to_mem cx, output_base
    add output_base, output_base, rb16

    reg_to_mem cy, output_base
    add output_base, output_base, rb16

    reg_to_mem cz, output_base
    add output_base, output_base, rb16

    reg_to_mem cw, output_base
    add output_base, output_base, rb16

    sub.setf num_iters, num_iters, 1
    brr.allnz -, :loop
    nop
    nop
    nop

# ---------- exit -------------------------------------------------
nop; thrend
nop
nop
