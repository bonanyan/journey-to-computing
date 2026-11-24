/* RV32I, ILP32 soft-float ABI. Each lane starts at address zero. */
.section .text.boot,"ax"
.globl _start
_start:
    li sp, 0x10000
    call dispatch
    li t0, 0x10000
    li t1, 1
    sw t1, 8(t0)             /* mailbox.done */
1:  j 1b

.text
.globl add_vector
/* a0=x, a1=y, a2=out, a3=count. __addsf3 takes FP32 bits in a0,a1. */
add_vector:
    addi sp, sp, -32
    sw ra, 0(sp)
    sw s0, 4(sp)
    sw s1, 8(sp)
    sw s2, 12(sp)
    sw s3, 16(sp)
    mv s0, a0
    mv s1, a1
    mv s2, a2
    mv s3, a3
.Ladd:
    beqz s3, .Ldone
    lw a0, 0(s0)
    lw a1, 0(s1)
    call __addsf3
    sw a0, 0(s2)
    addi s0, s0, 4
    addi s1, s1, 4
    addi s2, s2, 4
    addi s3, s3, -1
    j .Ladd
.Ldone:
    lw ra, 0(sp)
    lw s0, 4(sp)
    lw s1, 8(sp)
    lw s2, 12(sp)
    lw s3, 16(sp)
    addi sp, sp, 32
    ret

.globl dot_vector
/* a0=x, a1=y, a2=count. Return an FP32 bit pattern in a0. */
dot_vector:
    addi sp, sp, -32
    sw ra, 0(sp)
    sw s0, 4(sp)
    sw s1, 8(sp)
    sw s2, 12(sp)
    sw s3, 16(sp)
    mv s0, a0
    mv s1, a1
    mv s2, a2
    li s3, 0                 /* +0.0 */
.Ldot:
    beqz s2, .Ldot_done
    lw a0, 0(s0)
    lw a1, 0(s1)
    call __mulsf3
    mv a1, s3
    call __addsf3
    mv s3, a0
    addi s0, s0, 4
    addi s1, s1, 4
    addi s2, s2, -1
    j .Ldot
.Ldot_done:
    mv a0, s3
    lw ra, 0(sp)
    lw s0, 4(sp)
    lw s1, 8(sp)
    lw s2, 12(sp)
    lw s3, 16(sp)
    addi sp, sp, 32
    ret
