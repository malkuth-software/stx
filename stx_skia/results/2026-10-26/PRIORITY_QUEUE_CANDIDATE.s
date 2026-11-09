	.build_version macos, 26, 0	sdk_version 26, 2
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_queue_insert                   ; -- Begin function queue_insert
	.p2align	2
_queue_insert:                          ; @queue_insert
	.cfi_startproc
; %bb.0:
	stp	x22, x21, [sp, #-48]!           ; 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             ; 16-byte Folded Spill
	stp	x29, x30, [sp, #32]             ; 16-byte Folded Spill
	add	x29, sp, #32
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	.cfi_offset w19, -24
	.cfi_offset w20, -32
	.cfi_offset w21, -40
	.cfi_offset w22, -48
	mov	x19, x2
	mov	x20, x1
	mov	x21, x0
	ldr	w22, [x0, #20]
	bl	__ZN11SkTDStorage6appendEv
	ldr	x8, [x21, #8]
	cmp	w22, #1
	b.lt	LBB0_3
LBB0_1:                                 ; =>This Inner Loop Header: Depth=1
	sub	w9, w22, #1
	lsr	w9, w9, #1
	add	x10, x8, w9, uxtw #4
	ldr	w11, [x10]
	cmp	w11, w20
	b.le	LBB0_3
; %bb.2:                                ;   in Loop: Header=BB0_1 Depth=1
	ldr	q0, [x10]
	str	q0, [x8, w22, uxtw #4]
	mov	x22, x9
	cbnz	w9, LBB0_1
LBB0_3:
	add	x8, x8, w22, sxtw #4
	stp	x20, x19, [x8]
	ldp	x29, x30, [sp, #32]             ; 16-byte Folded Reload
	ldp	x20, x19, [sp, #16]             ; 16-byte Folded Reload
	ldp	x22, x21, [sp], #48             ; 16-byte Folded Reload
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_queue_pop                      ; -- Begin function queue_pop
	.p2align	6
_queue_pop:                             ; @queue_pop
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #48
	stp	x29, x30, [sp, #32]             ; 16-byte Folded Spill
	add	x29, sp, #32
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
Lloh0:
	adrp	x8, ___stack_chk_guard@GOTPAGE
Lloh1:
	ldr	x8, [x8, ___stack_chk_guard@GOTPAGEOFF]
Lloh2:
	ldr	x8, [x8]
	stur	x8, [x29, #-8]
	ldr	w11, [x0, #20]
	cmp	w11, #0
	b.le	LBB1_16
; %bb.1:
	subs	w10, w11, #1
	b.ne	LBB1_3
; %bb.2:
	str	wzr, [x0, #20]
	b	LBB1_14
LBB1_3:
	ldr	x8, [x0, #8]
	add	x12, x8, w10, uxtw #4
	ldr	w9, [x12]
	ldur	x13, [x12, #4]
	str	x13, [sp, #8]
	ldr	w12, [x12, #12]
	str	w12, [sp, #16]
	str	w10, [x0, #20]
	cmp	w11, #3
	b.lo	LBB1_10
; %bb.4:
	mov	w11, #0                         ; =0x0
	lsr	w12, w10, #1
LBB1_5:                                 ; =>This Inner Loop Header: Depth=1
	sbfiz	x14, x11, #1, #32
	sxtw	x13, w14
	orr	x13, x13, #0x1
	add	x14, x14, #2
	cmp	w14, w10
	b.ge	LBB1_8
; %bb.6:                                ;   in Loop: Header=BB1_5 Depth=1
	lsl	x15, x14, #4
	sbfiz	x16, x13, #4, #32
	ldr	w15, [x8, x15]
	ldr	w16, [x8, x16]
	.p2align	6, , 4
; %bb.7:                                ;   in Loop: Header=BB1_5 Depth=1
	cmp	w15, w16
	csel	w13, w14, w13, lt
	csel	w14, w15, w16, lt
	sxtw	x15, w13
                                        ; kill: def $w13 killed $w13 killed $x13 def $x13
	cmp	w14, w9
	b.lt	LBB1_9
	b	LBB1_11
LBB1_8:                                 ;   in Loop: Header=BB1_5 Depth=1
	lsl	x14, x13, #4
	ldr	w14, [x8, x14]
	mov	x15, x13
	cmp	w14, w9
	b.ge	LBB1_11
LBB1_9:                                 ;   in Loop: Header=BB1_5 Depth=1
	ldr	q0, [x8, x15, lsl #4]
	str	q0, [x8, w11, sxtw #4]
	mov	x11, x13
	cmp	w13, w12
	b.lt	LBB1_5
	b	LBB1_12
LBB1_10:
	mov	x10, #0                         ; =0x0
	b	LBB1_13
LBB1_11:
	mov	x13, x11
LBB1_12:
                                        ; kill: def $w13 killed $w13 killed $x13 def $x13
	sxtw	x10, w13
LBB1_13:
	add	x8, x8, x10, lsl #4
	str	w9, [x8]
	ldr	x9, [sp, #8]
	stur	x9, [x8, #4]
	ldr	w9, [sp, #16]
	str	w9, [x8, #12]
LBB1_14:
	ldur	x8, [x29, #-8]
Lloh3:
	adrp	x9, ___stack_chk_guard@GOTPAGE
Lloh4:
	ldr	x9, [x9, ___stack_chk_guard@GOTPAGEOFF]
Lloh5:
	ldr	x9, [x9]
	cmp	x9, x8
	b.ne	LBB1_17
; %bb.15:
	ldp	x29, x30, [sp, #32]             ; 16-byte Folded Reload
	add	sp, sp, #48
	ret
LBB1_16:
	bl	__ZZN10SkTDPQueueI5EntryXadL_ZL4lessRKS0_S2_EEE3popEvENKUlvE_clEv
LBB1_17:
	bl	___stack_chk_fail
	.loh AdrpLdrGotLdr	Lloh0, Lloh1, Lloh2
	.loh AdrpLdrGotLdr	Lloh3, Lloh4, Lloh5
	.cfi_endproc
                                        ; -- End function
	.p2align	2                               ; -- Begin function _ZZN10SkTDPQueueI5EntryXadL_ZL4lessRKS0_S2_EEE3popEvENKUlvE_clEv
__ZZN10SkTDPQueueI5EntryXadL_ZL4lessRKS0_S2_EEE3popEvENKUlvE_clEv: ; @_ZZN10SkTDPQueueI5EntryXadL_ZL4lessRKS0_S2_EEE3popEvENKUlvE_clEv
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #48
	stp	x29, x30, [sp, #32]             ; 16-byte Folded Spill
	add	x29, sp, #32
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
Lloh6:
	adrp	x8, l_.str.2@PAGE
Lloh7:
	add	x10, x8, l_.str.2@PAGEOFF
	mov	w8, #63                         ; =0x3f
Lloh8:
	adrp	x9, l_.str.1@PAGE
Lloh9:
	add	x9, x9, l_.str.1@PAGEOFF
	stp	x8, x10, [sp, #8]
	str	x9, [sp]
Lloh10:
	adrp	x1, l_.str@PAGE
Lloh11:
	add	x1, x1, l_.str@PAGEOFF
	mov	w0, #0                          ; =0x0
	bl	__Z5SkLog13SkLogPriorityPKcz
	bl	__Z17sk_abort_no_printv
	.loh AdrpAdd	Lloh10, Lloh11
	.loh AdrpAdd	Lloh8, Lloh9
	.loh AdrpAdd	Lloh6, Lloh7
	.cfi_endproc
                                        ; -- End function
	.section	__TEXT,__cstring,cstring_literals
l_.str:                                 ; @.str
	.asciz	"[skia] ** ERROR ** %s:%d: fatal error: \"check(%s)\"\n"

l_.str.1:                               ; @.str.1
	.asciz	"/Users/joshuahkuttenkuler/Developer/CodexBuilds/stx_skia-853b87f3f8f5/skia/src/core/SkTDPQueue.h"

l_.str.2:                               ; @.str.2
	.asciz	"oldCount > 0"

.subsections_via_symbols
