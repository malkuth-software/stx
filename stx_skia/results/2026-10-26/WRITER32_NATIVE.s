	.build_version macos, 26, 0	sdk_version 26, 2
	.section	__TEXT,__text,regular,pure_instructions
	.globl	__Z11append_wordR10SkWriter32i  ; -- Begin function _Z11append_wordR10SkWriter32i
	.p2align	2
__Z11append_wordR10SkWriter32i:         ; @_Z11append_wordR10SkWriter32i
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
	mov	x19, x1
	mov	x20, x0
	ldp	x8, x21, [x0, #8]
	add	x22, x21, #4
	cmp	x22, x8
	b.ls	LBB0_3
; %bb.1:
	tbnz	x22, #63, LBB0_4
LBB0_2:
	add	x1, x21, #4
	mov	x0, x20
	bl	__ZN10SkWriter3213growToAtLeastEm
LBB0_3:
	str	x22, [x20, #16]
	ldr	x8, [x20]
	str	w19, [x8, x21]
	ldp	x29, x30, [sp, #48]             ; 16-byte Folded Reload
	ldp	x20, x19, [sp, #32]             ; 16-byte Folded Reload
	ldp	x22, x21, [sp, #16]             ; 16-byte Folded Reload
	add	sp, sp, #64
	ret
LBB0_4:
	add	x0, sp, #15
	bl	__ZZN10SkWriter327reserveEmENKUlvE1_clEv
	b	LBB0_2
	.cfi_endproc
                                        ; -- End function
	.globl	__ZZN10SkWriter327reserveEmENKUlvE1_clEv ; -- Begin function _ZZN10SkWriter327reserveEmENKUlvE1_clEv
	.weak_def_can_be_hidden	__ZZN10SkWriter327reserveEmENKUlvE1_clEv
	.p2align	2
__ZZN10SkWriter327reserveEmENKUlvE1_clEv: ; @_ZZN10SkWriter327reserveEmENKUlvE1_clEv
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #48
	stp	x29, x30, [sp, #32]             ; 16-byte Folded Spill
	add	x29, sp, #32
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
Lloh0:
	adrp	x8, l_.str.4@PAGE
Lloh1:
	add	x10, x8, l_.str.4@PAGEOFF
	mov	w8, #76                         ; =0x4c
Lloh2:
	adrp	x9, l_.str.1@PAGE
Lloh3:
	add	x9, x9, l_.str.1@PAGEOFF
	stp	x8, x10, [sp, #8]
	str	x9, [sp]
Lloh4:
	adrp	x1, l_.str@PAGE
Lloh5:
	add	x1, x1, l_.str@PAGEOFF
	mov	w0, #0                          ; =0x0
	bl	__Z5SkLog13SkLogPriorityPKcz
	bl	__Z17sk_abort_no_printv
	.loh AdrpAdd	Lloh4, Lloh5
	.loh AdrpAdd	Lloh2, Lloh3
	.loh AdrpAdd	Lloh0, Lloh1
	.cfi_endproc
                                        ; -- End function
	.section	__TEXT,__cstring,cstring_literals
l_.str:                                 ; @.str
	.asciz	"[skia] ** ERROR ** %s:%d: fatal error: \"check(%s)\"\n"

l_.str.1:                               ; @.str.1
	.asciz	"skia/src/core/SkWriter32.h"

l_.str.4:                               ; @.str.4
	.asciz	"required <= MaximumExtent"

.subsections_via_symbols
