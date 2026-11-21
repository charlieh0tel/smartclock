/* The 58503A's flash programmer: CPU32 code that a BDM host loads into
 * the board's RAM and calls with the CPU held from a reset entry, so
 * that the flash's command writes and status polls run at bus speed
 * rather than one BDM command each (tools/bdm/README.md, "The routine").
 *
 * Position independent: every reference to its own code is PC
 * relative, so the host may load it anywhere in RAM; build_routine.py
 * refuses an object with a relocation.  The only absolute addresses
 * are the board's: the two flash pairs.  It uses no firmware, library
 * or RAM of its own but its stack, which the host sets.
 *
 * Interface (the host sets SR, SSP, VBR and the chip selects first):
 *
 *   in   D0.L  operation, OPERATION_*
 *        A0.L  flash address, even: the sector pair to erase, the
 *              first word to program or verify, any address in the
 *              pair to identify
 *        A1.L  RAM buffer, even: the words to program or verify;
 *              identify's results
 *        D1.L  words to program or verify, 1 to PAIR_WORDS, all in
 *              A0's pair
 *        D2.L  the lowest address erase or program may touch
 *        D3.L  the poll limit, at least 1: reads of a word's status
 *              before program gives up on it, or of the sector pair's
 *              before erase does
 *
 *   out  D0.L  status, STATUS_*
 *        A0.L  the failing word's address; on success the address
 *              after the last word done (identify: as given)
 *        D1.L  the word last read there (low word); 0 on success
 *        D2.L  the word expected there (low word); 0 on success
 *              (STATUS_BAD_OPERATION and STATUS_BAD_ARGUMENT name no
 *              word: A0, D1 and D2 are then as given)
 *
 *   An exception instead: D0 STATUS_EXCEPTION, A0 the stacked PC, D1
 *   the frame's format and vector word, D2 the fault address if the
 *   frame is a bus error's (format $C), else 0.
 *
 * Every path, success or failure, ends at finish: read/reset written
 * to both pairs, then BGND.  identify writes to its buffer the
 * manufacturer word, the device word and each of the pair's eight
 * sector pairs' protection words, ten words in all.
 *
 * Each word is both parts of a pair, one in each byte lane, and each
 * part finishes on its own schedule, so poll judges each lane's DQ7
 * and DQ5 on their own (Am29F010B, Write Operation Status, Figure 3,
 * p. 16; DQ5, p. 18). */

/* Operations. */
        .equ OPERATION_IDENTIFY, 1
        .equ OPERATION_ERASE, 2
        .equ OPERATION_PROGRAM, 3
        .equ OPERATION_VERIFY, 4

/* Statuses. */
        .equ STATUS_OK, 0
        .equ STATUS_BAD_OPERATION, 1
        .equ STATUS_BAD_ARGUMENT, 2
        /* a word to program has a 1 where the flash has a 0, which
         * only erase can make (Am29F010B, Byte Program Command
         * Sequence, p. 12); found before anything is programmed */
        .equ STATUS_NEEDS_ERASE, 3
        /* a lane's DQ5 set, and its DQ7 still wrong on a second read */
        .equ STATUS_EXCEEDED, 4
        /* the poll limit spent with a lane neither done nor DQ5 */
        .equ STATUS_TIMEOUT, 5
        /* a word of an erased sector pair not ERASED */
        .equ STATUS_NOT_BLANK, 6
        /* a word not what was programmed, or not the buffer's */
        .equ STATUS_MISMATCH, 7
        .equ STATUS_EXCEPTION, 8

/* The flash: two pairs of Am29F010s, word-interleaved, the even part
 * on D15-D8 and the odd on D7-D0, a part's address the bus address
 * less the pair's base, halved (docs/firmware/restart.md, "The
 * installer"; installer_scpi_diagnostic.h). */
        .equ PAIR_1_BASE, 0x00000
        .equ PAIR_2_BASE, 0x40000
        .equ FLASH_END, 0x80000
        .equ PAIR_SIZE, 0x40000
        .equ PAIR_WORDS, PAIR_SIZE / 2
        .equ PAIR_MASK, ~(PAIR_SIZE - 1)
        /* a part's 16 KiB sector, both parts' across the bus
         * (Am29F010B, Table 2, p. 9) */
        .equ SECTOR_PAIR_SIZE, 0x8000
        .equ SECTOR_PAIR_WORDS, SECTOR_PAIR_SIZE / 2
        .equ SECTORS_PER_PAIR, PAIR_SIZE / SECTOR_PAIR_SIZE
        .equ ERASED, 0xFFFF

/* The command cycles, each a part's command in both bytes, at a part's
 * 0x5555 and 0x2aaa (Am29F010B, Table 4, p. 15; the installer's,
 * installer_scpi_diagnostic.h). */
        .equ UNLOCK_FIRST, 0x5555 * 2
        .equ UNLOCK_SECOND, 0x2AAA * 2
        .equ UNLOCK_FIRST_DATA, 0xAAAA
        .equ UNLOCK_SECOND_DATA, 0x5555
        .equ AUTOSELECT, 0x9090
        .equ PROGRAM, 0xA0A0
        .equ ERASE_SETUP, 0x8080
        .equ SECTOR_ERASE, 0x3030
        .equ READ_RESET, 0xF0F0
        /* autoselect's reads: a part's 0x00 the manufacturer, 0x01 the
         * device, a sector's 0x02 its protection (Table 4) */
        .equ MANUFACTURER_OFFSET, 0x00 * 2
        .equ DEVICE_OFFSET, 0x01 * 2
        .equ PROTECTION_OFFSET, 0x02 * 2

/* Write operation status, both lanes (Am29F010B, Table 5, p. 18). */
        .equ DQ7_BOTH, 0x8080
        /* DQ5 to DQ7's place */
        .equ DQ5_TO_DQ7, 2

/* An exception's stack frame: SR, the PC and the format and vector
 * word, at the same place in every format, and a bus error's (format
 * $C) fault address after them (CPU32RM 6.4; MC68331UM 5.9.3). */
        .equ FRAME_PC, 2
        .equ FRAME_FORMAT_VECTOR, 6
        .equ FRAME_FAULT_ADDRESS, 8
        .equ FORMAT_MASK, 0xF000
        .equ FORMAT_BUS_ERROR, 0xC000

        .text
/* The two places the host enters: entry by its PC, exception by every
 * vector of the table it builds. */
entry:
        bra     main
exception:
        bra     trapped

main:
        /* A6 is 0 until finish starts, -1 after: an exception from
         * finish's own writes must not lead back to them. */
        suba.l  %a6,%a6
        move.l  %a0,%d7
        btst    #0,%d7
        bne     bad_argument
        cmpa.l  #FLASH_END,%a0
        bcc     bad_argument
        and.l   #PAIR_MASK,%d7
        movea.l %d7,%a2
        /* UNLOCK_FIRST is past a 16-bit displacement's reach */
        movea.l %a2,%a3
        adda.l  #UNLOCK_FIRST,%a3
        lea     UNLOCK_SECOND(%a2),%a4
        cmp.l   #OPERATION_IDENTIFY,%d0
        beq     identify
        cmp.l   #OPERATION_ERASE,%d0
        beq     erase
        cmp.l   #OPERATION_PROGRAM,%d0
        beq     program
        cmp.l   #OPERATION_VERIFY,%d0
        beq     verify
        moveq   #STATUS_BAD_OPERATION,%d0
        bra     finish

/* Autoselect, then the codes into the buffer. */
identify:
        move.l  %a1,%d7
        btst    #0,%d7
        bne     bad_argument
        move.w  #AUTOSELECT,%d0
        bsr     command
        move.w  MANUFACTURER_OFFSET(%a2),(%a1)+
        move.w  DEVICE_OFFSET(%a2),(%a1)+
        lea     PROTECTION_OFFSET(%a2),%a5
        moveq   #SECTORS_PER_PAIR - 1,%d7
1:      move.w  (%a5),(%a1)+
        adda.l  #SECTOR_PAIR_SIZE,%a5
        dbra    %d7,1b
        bra     succeeded

/* One sector pair, A0 its base; then every word of it read back. */
erase:
        cmpa.l  %d2,%a0
        bcs     bad_argument
        move.l  %a0,%d7
        and.l   #SECTOR_PAIR_SIZE - 1,%d7
        bne     bad_argument
        tst.l   %d3
        beq     bad_argument
        move.w  #ERASE_SETUP,%d0
        bsr     command
        bsr     unlock
        move.w  #SECTOR_ERASE,(%a0)
        movea.l %a0,%a5
        move.w  #ERASED,%d5
        move.l  %d3,%d6
        bsr     poll
        tst.l   %d0
        bne     poll_failed
        move.w  #SECTOR_PAIR_WORDS - 1,%d7
1:      move.w  (%a0)+,%d1
        cmp.w   #ERASED,%d1
        dbne    %d7,1b
        beq     succeeded
        subq.l  #2,%a0
        move.l  #ERASED,%d2
        moveq   #STATUS_NOT_BLANK,%d0
        bra     finish

/* D1 words from the buffer, each only after the whole run is found
 * programmable: a word already equal is skipped. */
program:
        cmpa.l  %d2,%a0
        bcs     bad_argument
        tst.l   %d3
        beq     bad_argument
        bsr     check_run
        bne     finish
        movem.l %d4/%a0-%a1,-(%sp)
1:      move.w  (%a0)+,%d1
        move.w  (%a1)+,%d2
        move.w  %d1,%d7
        not.w   %d7
        and.w   %d2,%d7
        bne     needs_erase
        subq.l  #1,%d4
        bne     1b
        movem.l (%sp)+,%d4/%a0-%a1
2:      move.w  (%a1)+,%d5
        cmp.w   (%a0),%d5
        beq     3f
        move.w  #PROGRAM,%d0
        bsr     command
        move.w  %d5,(%a0)
        movea.l %a0,%a5
        move.l  %d3,%d6
        bsr     poll
        tst.l   %d0
        bne     poll_failed
        cmp.w   %d1,%d5
        bne     program_mismatch
3:      addq.l  #2,%a0
        subq.l  #1,%d4
        bne     2b
        bra     succeeded
needs_erase:
        lea     12(%sp),%sp
        subq.l  #2,%a0
        moveq   #STATUS_NEEDS_ERASE,%d0
        bra     finish
program_mismatch:
        moveq   #0,%d2
        move.w  %d5,%d2
        moveq   #STATUS_MISMATCH,%d0
        bra     finish

/* D1 words against the buffer. */
verify:
        bsr     check_run
        bne     finish
1:      move.w  (%a0)+,%d1
        move.w  (%a1)+,%d2
        cmp.w   %d1,%d2
        bne     2f
        subq.l  #1,%d4
        bne     1b
        bra     succeeded
2:      subq.l  #2,%a0
        moveq   #STATUS_MISMATCH,%d0
        bra     finish

/* Checks program's and verify's buffer and run: D0 STATUS_OK, the
 * count in D4, or STATUS_BAD_ARGUMENT, with the flags D0's. */
check_run:
        move.l  %a1,%d7
        btst    #0,%d7
        bne     1f
        move.l  %d1,%d4
        beq     1f
        cmp.l   #PAIR_WORDS,%d4
        bhi     1f
        /* the last word's address: in A0's pair, which lies below
         * FLASH_END */
        move.l  %d4,%d7
        subq.l  #1,%d7
        add.l   %d7,%d7
        add.l   %a0,%d7
        move.l  %a0,%d0
        eor.l   %d7,%d0
        and.l   #PAIR_MASK,%d0
        bne     1f
        moveq   #STATUS_OK,%d0
        rts
1:      moveq   #STATUS_BAD_ARGUMENT,%d0
        rts

/* The two unlock cycles to the pair; command adds D0 at the first
 * unlock address. */
command:
        bsr     unlock
        move.w  %d0,(%a3)
        rts
unlock:
        move.w  #UNLOCK_FIRST_DATA,(%a3)
        move.w  #UNLOCK_SECOND_DATA,(%a4)
        rts

/* Data polling, each lane on its own, at A5 for the word D5, at most
 * D6 reads: D0 STATUS_OK, the word then read in D1; else
 * STATUS_EXCEEDED or STATUS_TIMEOUT, the last read in D1.  A lane is
 * done when its DQ7 is D5's; one not done showing DQ5 is read again,
 * since DQ7 may change with DQ5, and fails if still not done (Figure 3
 * and its note 2).  Once both are done the word is read once more,
 * since DQ7 may change before DQ6-DQ0 do (p. 16). */
poll:
        move.w  (%a5),%d1
        move.w  %d1,%d7
        eor.w   %d5,%d7
        and.w   #DQ7_BOTH,%d7
        beq     2f
        move.w  %d1,%d0
        lsl.w   #DQ5_TO_DQ7,%d0
        and.w   %d7,%d0
        beq     1f
        move.w  (%a5),%d1
        move.w  %d1,%d7
        eor.w   %d5,%d7
        and.w   %d0,%d7
        bne     3f
1:      subq.l  #1,%d6
        bne     poll
        moveq   #STATUS_TIMEOUT,%d0
        rts
2:      move.w  (%a5),%d1
        moveq   #STATUS_OK,%d0
        rts
3:      moveq   #STATUS_EXCEEDED,%d0
        rts

poll_failed:
        movea.l %a5,%a0
        moveq   #0,%d2
        move.w  %d5,%d2
        bra     finish

bad_argument:
        moveq   #STATUS_BAD_ARGUMENT,%d0
        bra     finish

succeeded:
        moveq   #STATUS_OK,%d0
        moveq   #0,%d1
        moveq   #0,%d2

/* Both pairs back to reading array data, then background mode.  A
 * part busy erasing or programming ignores the reset (Am29F010B, Reset
 * Command, p. 12). */
finish:
        moveq   #-1,%d7
        movea.l %d7,%a6
        move.w  #READ_RESET,(PAIR_1_BASE).l
        move.w  #READ_RESET,(PAIR_2_BASE).l
halt:
        bgnd
        bra     halt

/* Every vector's: the frame's PC and format and vector word, and a bus
 * error's fault address, then finish, unless finish itself faulted. */
trapped:
        movea.l FRAME_PC(%sp),%a0
        moveq   #0,%d1
        move.w  FRAME_FORMAT_VECTOR(%sp),%d1
        moveq   #0,%d2
        move.w  %d1,%d7
        and.w   #FORMAT_MASK,%d7
        cmp.w   #FORMAT_BUS_ERROR,%d7
        bne     1f
        move.l  FRAME_FAULT_ADDRESS(%sp),%d2
1:      moveq   #STATUS_EXCEPTION,%d0
        move.l  %a6,%d7
        addq.l  #1,%d7
        beq     halt
        bra     finish
routine_end:
