	.build_version macos, 26, 0	sdk_version 26, 2
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_queue_insert                   ; -- Begin function queue_insert
	.p2align	2
_queue_insert:                          ; @queue_insert
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #64
	stp	x22, x21, [sp, #16]             ; 16-byte Folded Spill
	stp	x20, x19, [sp, #32]             ; 16-byte Folded Spill
	stp	x29, x30, [sp, #48]             ; 16-byte Folded Spill
	add	x29, sp, #48
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	.cfi_offset w19, -24
	.cfi_offset w20, -32
	.cfi_offset w21, -40
	.cfi_offset w22, -48
	mov	x20, x2
	mov	x21, x1
	mov	x19, x0
	ldr	w22, [x0, #20]
	bl	__ZN11SkTDStorage6appendEv
	ldr	x8, [x19, #8]
	ldrsw	x9, [x19, #20]
	add	x8, x8, x9, lsl #4
	stp	x21, x20, [x8, #-16]
	cbz	w22, LBB0_11
; %bb.1:
	tbnz	w22, #31, LBB0_12
; %bb.2:
	ldr	w9, [x19, #20]
	cmp	w22, w9
	b.ge	LBB0_12
; %bb.3:
	sub	w8, w22, #1
	asr	w8, w8, #1
	tbnz	w8, #31, LBB0_12
; %bb.4:
	cmp	w8, w9
	b.ge	LBB0_12
; %bb.5:
	ldr	x10, [x19, #8]
	add	x9, x10, x22, lsl #4
	add	x10, x10, w8, uxtw #4
	ldr	w11, [x9]
	ldr	w12, [x10]
	cmp	w11, w12
	b.ge	LBB0_11
; %bb.6:
	ldr	q0, [x9]
	ldr	q1, [x10]
	str	q1, [x9]
	str	q0, [x10]
	cbz	w8, LBB0_11
LBB0_7:                                 ; =>This Inner Loop Header: Depth=1
	ldr	w10, [x19, #20]
	cmp	w8, w10
	b.ge	LBB0_12
; %bb.8:                                ;   in Loop: Header=BB0_7 Depth=1
	sub	w9, w8, #1
	lsr	w9, w9, #1
	cmp	w9, w10
	b.ge	LBB0_12
; %bb.9:                                ;   in Loop: Header=BB0_7 Depth=1
	ldr	x11, [x19, #8]
	add	x10, x11, w8, uxtw #4
	add	x8, x11, w9, uxtw #4
	ldr	w11, [x10]
	ldr	w12, [x8]
	cmp	w11, w12
	b.ge	LBB0_11
; %bb.10:                               ;   in Loop: Header=BB0_7 Depth=1
	ldr	q0, [x10]
	ldr	q1, [x8]
	str	q1, [x10]
	str	q0, [x8]
	mov	x8, x9
	cbnz	w9, LBB0_7
LBB0_11:
	ldp	x29, x30, [sp, #48]             ; 16-byte Folded Reload
	ldp	x20, x19, [sp, #32]             ; 16-byte Folded Reload
	ldp	x22, x21, [sp, #16]             ; 16-byte Folded Reload
	add	sp, sp, #64
	ret
LBB0_12:
	brk	#0x1
	.cfi_endproc
                                        ; -- End function
	.globl	_queue_pop                      ; -- Begin function queue_pop
	.p2align	6
_queue_pop:                             ; @queue_pop
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #32
	.cfi_def_cfa_offset 32
	ldr	w8, [x0, #20]
	cmp	w8, #1
	b.ne	LBB1_2
; %bb.1:
	str	wzr, [x0, #20]
	add	sp, sp, #32
	ret
LBB1_2:
	cmp	w8, #0
	b.le	LBB1_19
; %bb.3:
	ldr	x9, [x0, #8]
	add	x8, x9, x8, lsl #4
	ldur	q0, [x8, #-16]
	str	q0, [x9]
	ldr	w9, [x0, #20]
	sub	w8, w9, #1
	str	w8, [x0, #20]
	cmp	w9, #3
	b.lt	LBB1_17
; %bb.4:
	mov	w11, #0                         ; =0x0
	mov	w9, #0                          ; =0x0
	mov	w10, #1                         ; =0x1
LBB1_5:                                 ; =>This Inner Loop Header: Depth=1
	add	w12, w11, #2
	cmp	w12, w8
	b.ge	LBB1_10
; %bb.6:                                ;   in Loop: Header=BB1_5 Depth=1
	cmn	w9, #2
	b.le	LBB1_19
; %bb.7:                                ;   in Loop: Header=BB1_5 Depth=1
	tbnz	w9, #31, LBB1_19
; %bb.8:                                ;   in Loop: Header=BB1_5 Depth=1
	ldr	x11, [x0, #8]
	ubfiz	x13, x12, #4, #32
	ldr	w13, [x11, x13]
	ubfiz	x14, x10, #4, #32
	ldr	w14, [x11, x14]
	.p2align	6, , 4
; %bb.9:                                ;   in Loop: Header=BB1_5 Depth=1
	cmp	w13, w14
	csel	w12, w12, w10, lt
	csel	w13, w13, w14, lt
	cmp	w12, w8
	b.lt	LBB1_14
	b	LBB1_19
LBB1_10:                                ;   in Loop: Header=BB1_5 Depth=1
	tbnz	w9, #31, LBB1_19
; %bb.11:                               ;   in Loop: Header=BB1_5 Depth=1
	cmp	w9, w8
	b.ge	LBB1_19
; %bb.12:                               ;   in Loop: Header=BB1_5 Depth=1
	ldr	x11, [x0, #8]
	ubfiz	x12, x10, #4, #32
	ubfiz	x14, x9, #4, #32
	ldr	w13, [x11, x12]
	ldr	w12, [x11, x14]
	cmp	w13, w12
	b.lt	LBB1_18
; %bb.13:                               ;   in Loop: Header=BB1_5 Depth=1
	mov	x12, x10
	cmp	w12, w8
	b.ge	LBB1_19
LBB1_14:                                ;   in Loop: Header=BB1_5 Depth=1
	cmp	w9, w8
	b.ge	LBB1_19
; %bb.15:                               ;   in Loop: Header=BB1_5 Depth=1
	add	x8, x11, w9, uxtw #4
	ldr	w9, [x8]
	cmp	w13, w9
	b.ge	LBB1_17
; %bb.16:                               ;   in Loop: Header=BB1_5 Depth=1
	ldr	q0, [x11, w12, uxtw #4]
	ldr	q1, [x8]
	str	q1, [x11, w12, uxtw #4]
	str	q0, [x8]
	lsl	w11, w12, #1
	mov	w10, #1                         ; =0x1
	bfi	w10, w12, #1, #31
	ldr	w8, [x0, #20]
	mov	x9, x12
	cmp	w10, w8
	b.lt	LBB1_5
LBB1_17:
	add	sp, sp, #32
	ret
LBB1_18:
	mov	w8, w10
	mov	w9, w9
	lsl	x8, x8, #4
	lsl	x9, x9, #4
	ldr	q0, [x11, x8]
	ldr	q1, [x11, x9]
	str	q1, [x11, x8]
	str	q0, [x11, x9]
	add	sp, sp, #32
	ret
LBB1_19:
	brk	#0x1
	.cfi_endproc
                                        ; -- End function
.subsections_via_symbols
