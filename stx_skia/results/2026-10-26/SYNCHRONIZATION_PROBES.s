	.build_version macos, 26, 0	sdk_version 26, 2
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_semaphore_cycle                ; -- Begin function semaphore_cycle
	.p2align	2
_semaphore_cycle:                       ; @semaphore_cycle
	.cfi_startproc
; %bb.0:
	stp	x20, x19, [sp, #-32]!           ; 16-byte Folded Spill
	stp	x29, x30, [sp, #16]             ; 16-byte Folded Spill
	add	x29, sp, #16
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	.cfi_offset w19, -24
	.cfi_offset w20, -32
	mov	w8, #-1                         ; =0xffffffff
	ldadda	w8, w8, [x0]
	cmp	w8, #0
	b.le	LBB0_3
; %bb.1:
	mov	w8, #1                          ; =0x1
	ldaddl	w8, w8, [x0]
	tbnz	w8, #31, LBB0_4
LBB0_2:
	ldp	x29, x30, [sp, #16]             ; 16-byte Folded Reload
	ldp	x20, x19, [sp], #32             ; 16-byte Folded Reload
	ret
LBB0_3:
	mov	x19, x0
	bl	__ZN11SkSemaphore6osWaitEv
	mov	x0, x19
	mov	w8, #1                          ; =0x1
	ldaddl	w8, w8, [x19]
	tbz	w8, #31, LBB0_2
LBB0_4:
	mov	w1, #1                          ; =0x1
	ldp	x29, x30, [sp, #16]             ; 16-byte Folded Reload
	ldp	x20, x19, [sp], #32             ; 16-byte Folded Reload
	b	__ZN11SkSemaphore8osSignalEi
	.cfi_endproc
                                        ; -- End function
	.globl	_mutex_cycle                    ; -- Begin function mutex_cycle
	.p2align	2
_mutex_cycle:                           ; @mutex_cycle
Lfunc_begin0:
	.cfi_startproc
	.cfi_personality 155, ___gxx_personality_v0
	.cfi_lsda 16, Lexception0
; %bb.0:
	stp	x20, x19, [sp, #-32]!           ; 16-byte Folded Spill
	stp	x29, x30, [sp, #16]             ; 16-byte Folded Spill
	add	x29, sp, #16
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	.cfi_offset w19, -24
	.cfi_offset w20, -32
	mov	w8, #-1                         ; =0xffffffff
	ldadda	w8, w8, [x0]
	cmp	w8, #0
	b.le	LBB1_2
; %bb.1:
	mov	w8, #1                          ; =0x1
	ldaddl	w8, w8, [x0]
	tbz	w8, #31, LBB1_4
	b	LBB1_3
LBB1_2:
	mov	x19, x0
	bl	__ZN11SkSemaphore6osWaitEv
	mov	x0, x19
	mov	w8, #1                          ; =0x1
	ldaddl	w8, w8, [x19]
	tbz	w8, #31, LBB1_4
LBB1_3:
Ltmp0:                                  ; EH_LABEL
	mov	w1, #1                          ; =0x1
	bl	__ZN11SkSemaphore8osSignalEi
Ltmp1:                                  ; EH_LABEL
LBB1_4:
	ldp	x29, x30, [sp, #16]             ; 16-byte Folded Reload
	ldp	x20, x19, [sp], #32             ; 16-byte Folded Reload
	ret
LBB1_5:
Ltmp2:                                  ; EH_LABEL
	bl	___clang_call_terminate
Lfunc_end0:
	.cfi_endproc
	.section	__TEXT,__gcc_except_tab
	.p2align	2, 0x0
GCC_except_table1:
Lexception0:
	.byte	255                             ; @LPStart Encoding = omit
	.byte	155                             ; @TType Encoding = indirect pcrel sdata4
	.uleb128 Lttbase0-Lttbaseref0
Lttbaseref0:
	.byte	1                               ; Call site Encoding = uleb128
	.uleb128 Lcst_end0-Lcst_begin0
Lcst_begin0:
	.uleb128 Lfunc_begin0-Lfunc_begin0      ; >> Call Site 1 <<
	.uleb128 Ltmp0-Lfunc_begin0             ;   Call between Lfunc_begin0 and Ltmp0
	.byte	0                               ;     has no landing pad
	.byte	0                               ;   On action: cleanup
	.uleb128 Ltmp0-Lfunc_begin0             ; >> Call Site 2 <<
	.uleb128 Ltmp1-Ltmp0                    ;   Call between Ltmp0 and Ltmp1
	.uleb128 Ltmp2-Lfunc_begin0             ;     jumps to Ltmp2
	.byte	1                               ;   On action: 1
Lcst_end0:
	.byte	1                               ; >> Action Record 1 <<
                                        ;   Catch TypeInfo 1
	.byte	0                               ;   No further actions
	.p2align	2, 0x0
                                        ; >> Catch TypeInfos <<
	.long	0                               ; TypeInfo 1
Lttbase0:
	.p2align	2, 0x0
                                        ; -- End function
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_once_call                      ; -- Begin function once_call
	.p2align	2
_once_call:                             ; @once_call
	.cfi_startproc
; %bb.0:
	ldaprb	w8, [x0]
	cbnz	w8, LBB2_4
; %bb.1:
	mov	w9, #1                          ; =0x1
	casb	w8, w9, [x0]
	cbnz	w8, LBB2_3
; %bb.2:
	mov	w8, #17                         ; =0x11
	str	w8, [x1]
	mov	w8, #2                          ; =0x2
	stlrb	w8, [x0]
	ret
LBB2_3:
	ldaprb	w8, [x0]
LBB2_4:
	cmp	w8, #2
	b.ne	LBB2_3
; %bb.5:
	ret
	.cfi_endproc
                                        ; -- End function
	.private_extern	___clang_call_terminate ; -- Begin function __clang_call_terminate
	.globl	___clang_call_terminate
	.weak_def_can_be_hidden	___clang_call_terminate
	.p2align	2
___clang_call_terminate:                ; @__clang_call_terminate
	.cfi_startproc
; %bb.0:
	stp	x29, x30, [sp, #-16]!           ; 16-byte Folded Spill
	mov	x29, sp
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	bl	___cxa_begin_catch
	bl	__ZSt9terminatev
	.cfi_endproc
                                        ; -- End function
.subsections_via_symbols
