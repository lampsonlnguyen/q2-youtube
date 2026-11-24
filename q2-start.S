/* Bridge the MIPS o32 process entry stack to glibc's __libc_start_main.
 * Use the Q2's NaN2008 loader and libc rather than the host cross-toolchain CRT. */
    .text
    .set noreorder
    .globl __start
    .ent __start
__start:
    lui $gp, %hi(_gp)
    addiu $gp, $gp, %lo(_gp)
    move $ra, $zero
    lw $a1, 0($sp)
    addiu $a2, $sp, 4
    li $t0, -8
    and $sp, $sp, $t0
    addiu $sp, $sp, -32
    lui $a0, %hi(main)
    addiu $a0, $a0, %lo(main)
    move $a3, $zero
    sw $zero, 16($sp)
    sw $v0, 20($sp)
    sw $sp, 24($sp)
    lw $t9, %call16(__libc_start_main)($gp)
    jalr $t9
    nop
1:
    b 1b
    nop
    .end __start
    .set reorder

    .section .rodata
    .align 2
    .globl _IO_stdin_used
_IO_stdin_used:
    .word 0x20001
