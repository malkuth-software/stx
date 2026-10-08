
.build/stx-perf/CMakeFiles/stx_vorbis.dir/src/synthesis.cpp.obj:	file format coff-x86-64

Disassembly of section .text:

0000000000000000 <stx_vorbis::synthesis_available(stx_vorbis::Synthesis)>:
       0: b0 01                        	movb	$0x1, %al
       2: 80 f9 02                     	cmpb	$0x2, %cl
       5: 72 1c                        	jb	0x23 <stx_vorbis::synthesis_available(stx_vorbis::Synthesis)+0x23>
       7: 80 f9 03                     	cmpb	$0x3, %cl
       a: 74 17                        	je	0x23 <stx_vorbis::synthesis_available(stx_vorbis::Synthesis)+0x23>
       c: 0f b6 c1                     	movzbl	%cl, %eax
       f: 83 f8 04                     	cmpl	$0x4, %eax
      12: 75 0d                        	jne	0x21 <stx_vorbis::synthesis_available(stx_vorbis::Synthesis)+0x21>
      14: 0f b6 05 0d 00 00 00         	movzbl	0xd(%rip), %eax         # 0x28 <stx_vorbis::synthesis_available(stx_vorbis::Synthesis)+0x28>
      1b: 24 04                        	andb	$0x4, %al
      1d: c0 e8 02                     	shrb	$0x2, %al
      20: c3                           	retq
      21: 31 c0                        	xorl	%eax, %eax
      23: c3                           	retq
      24: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)

0000000000000030 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)>:
      30: 41 57                        	pushq	%r15
      32: 41 56                        	pushq	%r14
      34: 41 55                        	pushq	%r13
      36: 41 54                        	pushq	%r12
      38: 56                           	pushq	%rsi
      39: 57                           	pushq	%rdi
      3a: 55                           	pushq	%rbp
      3b: 53                           	pushq	%rbx
      3c: 48 81 ec 98 00 00 00         	subq	$0x98, %rsp
      43: 66 44 0f 29 94 24 80 00 00 00	movapd	%xmm10, 0x80(%rsp)
      4d: 66 44 0f 29 4c 24 70         	movapd	%xmm9, 0x70(%rsp)
      54: 66 44 0f 29 44 24 60         	movapd	%xmm8, 0x60(%rsp)
      5b: 66 0f 7f 7c 24 50            	movdqa	%xmm7, 0x50(%rsp)
      61: 66 0f 29 74 24 40            	movapd	%xmm6, 0x40(%rsp)
      67: 89 d7                        	movl	%edx, %edi
      69: 48 89 cb                     	movq	%rcx, %rbx
      6c: 89 11                        	movl	%edx, (%rcx)
      6e: 89 51 04                     	movl	%edx, 0x4(%rcx)
      71: 8d 77 ff                     	leal	-0x1(%rdi), %esi
      74: 4c 8d 71 08                  	leaq	0x8(%rcx), %r14
      78: 41 89 d7                     	movl	%edx, %r15d
      7b: 4c 89 f1                     	movq	%r14, %rcx
      7e: 4c 89 fa                     	movq	%r15, %rdx
      81: e8 00 00 00 00               	callq	0x86 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x56>
      86: 48 89 7c 24 38               	movq	%rdi, 0x38(%rsp)
      8b: 89 fd                        	movl	%edi, %ebp
      8d: 83 ed 01                     	subl	$0x1, %ebp
      90: 0f 82 cf 00 00 00            	jb	0x165 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x135>
      96: 49 8b 0e                     	movq	(%r14), %rcx
      99: 0f 84 b7 00 00 00            	je	0x156 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x126>
      9f: 0f bd c6                     	bsrl	%esi, %eax
      a2: 83 f0 1f                     	xorl	$0x1f, %eax
      a5: ba 20 00 00 00               	movl	$0x20, %edx
      aa: 29 c2                        	subl	%eax, %edx
      ac: 83 f0 1f                     	xorl	$0x1f, %eax
      af: 41 89 d0                     	movl	%edx, %r8d
      b2: 41 83 e0 03                  	andl	$0x3, %r8d
      b6: 83 e2 fc                     	andl	$-0x4, %edx
      b9: 45 31 c9                     	xorl	%r9d, %r9d
      bc: eb 12                        	jmp	0xd0 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0xa0>
      be: 66 90                        	nop
      c0: 46 89 1c 89                  	movl	%r11d, (%rcx,%r9,4)
      c4: 49 ff c1                     	incq	%r9
      c7: 4d 39 f9                     	cmpq	%r15, %r9
      ca: 0f 84 95 00 00 00            	je	0x165 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x135>
      d0: 83 f8 03                     	cmpl	$0x3, %eax
      d3: 73 0b                        	jae	0xe0 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0xb0>
      d5: 45 89 ca                     	movl	%r9d, %r10d
      d8: 45 31 db                     	xorl	%r11d, %r11d
      db: eb 52                        	jmp	0x12f <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0xff>
      dd: 0f 1f 00                     	nopl	(%rax)
      e0: 45 31 db                     	xorl	%r11d, %r11d
      e3: 89 d6                        	movl	%edx, %esi
      e5: 45 89 ca                     	movl	%r9d, %r10d
      e8: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
      f0: 44 89 d7                     	movl	%r10d, %edi
      f3: 83 e7 01                     	andl	$0x1, %edi
      f6: c1 e7 02                     	shll	$0x2, %edi
      f9: 46 8d 1c df                  	leal	(%rdi,%r11,8), %r11d
      fd: 44 89 d7                     	movl	%r10d, %edi
     100: 83 e7 02                     	andl	$0x2, %edi
     103: 44 09 df                     	orl	%r11d, %edi
     106: 45 89 d3                     	movl	%r10d, %r11d
     109: 41 c1 eb 02                  	shrl	$0x2, %r11d
     10d: 41 83 e3 01                  	andl	$0x1, %r11d
     111: 41 09 fb                     	orl	%edi, %r11d
     114: 44 89 d7                     	movl	%r10d, %edi
     117: c1 ef 03                     	shrl	$0x3, %edi
     11a: 83 e7 01                     	andl	$0x1, %edi
     11d: 46 8d 1c 5f                  	leal	(%rdi,%r11,2), %r11d
     121: 41 c1 ea 04                  	shrl	$0x4, %r10d
     125: 83 c6 fc                     	addl	$-0x4, %esi
     128: 75 c6                        	jne	0xf0 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0xc0>
     12a: 45 85 c0                     	testl	%r8d, %r8d
     12d: 74 91                        	je	0xc0 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x90>
     12f: 44 89 c6                     	movl	%r8d, %esi
     132: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
     140: 44 89 d7                     	movl	%r10d, %edi
     143: 83 e7 01                     	andl	$0x1, %edi
     146: 46 8d 1c 5f                  	leal	(%rdi,%r11,2), %r11d
     14a: 41 d1 ea                     	shrl	%r10d
     14d: ff ce                        	decl	%esi
     14f: 75 ef                        	jne	0x140 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x110>
     151: e9 6a ff ff ff               	jmp	0xc0 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x90>
     156: 4e 8d 04 bd 00 00 00 00      	leaq	(,%r15,4), %r8
     15e: 31 d2                        	xorl	%edx, %edx
     160: e8 00 00 00 00               	callq	0x165 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x135>
     165: 4c 89 7c 24 28               	movq	%r15, 0x28(%rsp)
     16a: 4c 8d 73 28                  	leaq	0x28(%rbx), %r14
     16e: 41 89 ec                     	movl	%ebp, %r12d
     171: 4c 89 f1                     	movq	%r14, %rcx
     174: 4c 89 e2                     	movq	%r12, %rdx
     177: e8 00 00 00 00               	callq	0x17c <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x14c>
     17c: 48 89 5c 24 30               	movq	%rbx, 0x30(%rsp)
     181: 4c 8d 7b 48                  	leaq	0x48(%rbx), %r15
     185: 4c 89 f9                     	movq	%r15, %rcx
     188: 4c 89 e2                     	movq	%r12, %rdx
     18b: e8 00 00 00 00               	callq	0x190 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x160>
     190: 48 8b 5c 24 38               	movq	0x38(%rsp), %rbx
     195: 83 fb 02                     	cmpl	$0x2, %ebx
     198: 0f 83 81 00 00 00            	jae	0x21f <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x1ef>
     19e: 48 8b 74 24 30               	movq	0x30(%rsp), %rsi
     1a3: 4c 8d 6e 68                  	leaq	0x68(%rsi), %r13
     1a7: 41 89 de                     	movl	%ebx, %r14d
     1aa: 41 d1 ee                     	shrl	%r14d
     1ad: 4c 89 e9                     	movq	%r13, %rcx
     1b0: 4c 89 f2                     	movq	%r14, %rdx
     1b3: e8 00 00 00 00               	callq	0x1b8 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x188>
     1b8: 48 8d ae 88 00 00 00         	leaq	0x88(%rsi), %rbp
     1bf: 48 89 e9                     	movq	%rbp, %rcx
     1c2: 4c 89 f2                     	movq	%r14, %rdx
     1c5: e8 00 00 00 00               	callq	0x1ca <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x19a>
     1ca: 48 8d 8e a8 00 00 00         	leaq	0xa8(%rsi), %rcx
     1d1: 49 89 cc                     	movq	%rcx, %r12
     1d4: 48 8b 7c 24 28               	movq	0x28(%rsp), %rdi
     1d9: 48 89 fa                     	movq	%rdi, %rdx
     1dc: e8 00 00 00 00               	callq	0x1e1 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x1b1>
     1e1: 48 81 c6 c8 00 00 00         	addq	$0xc8, %rsi
     1e8: 48 89 f1                     	movq	%rsi, %rcx
     1eb: 48 89 fa                     	movq	%rdi, %rdx
     1ee: e8 00 00 00 00               	callq	0x1f3 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x1c3>
     1f3: 45 85 f6                     	testl	%r14d, %r14d
     1f6: 0f 84 34 02 00 00            	je	0x430 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x400>
     1fc: 0f 57 ff                     	xorps	%xmm7, %xmm7
     1ff: f2 48 0f 2a 7c 24 28         	cvtsi2sdq	0x28(%rsp), %xmm7
     206: 4d 8b 7d 00                  	movq	(%r13), %r15
     20a: 4c 8b 6d 00                  	movq	(%rbp), %r13
     20e: 41 83 fe 01                  	cmpl	$0x1, %r14d
     212: 0f 85 40 01 00 00            	jne	0x358 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x328>
     218: 31 ed                        	xorl	%ebp, %ebp
     21a: e9 c5 01 00 00               	jmp	0x3e4 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x3b4>
     21f: 4d 8b 36                     	movq	(%r14), %r14
     222: 4d 8b 3f                     	movq	(%r15), %r15
     225: bd 02 00 00 00               	movl	$0x2, %ebp
     22a: f3 0f 7e 3d 10 00 00 00      	movq	0x10(%rip), %xmm7       # 0x242 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x212>
     232: 66 44 0f 28 05 20 00 00 00   	movapd	0x20(%rip), %xmm8       # 0x25b <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x22b>
     23b: f2 44 0f 10 0d 00 00 00 00   	movsd	(%rip), %xmm9           # 0x244 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x214>
     244: eb 5e                        	jmp	0x2a4 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x274>
     246: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
     250: 45 31 ed                     	xorl	%r13d, %r13d
     253: ff ce                        	decl	%esi
     255: 66 49 0f 6e c5               	movq	%r13, %xmm0
     25a: 66 0f 62 c7                  	punpckldq	%xmm7, %xmm0    # xmm0 = xmm0[0],xmm7[0],xmm0[1],xmm7[1]
     25e: 66 41 0f 5c c0               	subpd	%xmm8, %xmm0
     263: 66 0f 28 f0                  	movapd	%xmm0, %xmm6
     267: 66 0f 15 f0                  	unpckhpd	%xmm0, %xmm6            # xmm6 = xmm6[1],xmm0[1]
     26b: f2 0f 58 f0                  	addsd	%xmm0, %xmm6
     26f: f2 41 0f 59 f1               	mulsd	%xmm9, %xmm6
     274: f2 41 0f 5e f2               	divsd	%xmm10, %xmm6
     279: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     27d: e8 00 00 00 00               	callq	0x282 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x252>
     282: 44 01 ee                     	addl	%r13d, %esi
     285: f2 41 0f 11 04 f6            	movsd	%xmm0, (%r14,%rsi,8)
     28b: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     28f: e8 00 00 00 00               	callq	0x294 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x264>
     294: f2 41 0f 11 04 f7            	movsd	%xmm0, (%r15,%rsi,8)
     29a: 01 ed                        	addl	%ebp, %ebp
     29c: 39 dd                        	cmpl	%ebx, %ebp
     29e: 0f 87 fa fe ff ff            	ja	0x19e <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x16e>
     2a4: 85 ed                        	testl	%ebp, %ebp
     2a6: 74 f2                        	je	0x29a <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x26a>
     2a8: 89 ee                        	movl	%ebp, %esi
     2aa: d1 ee                        	shrl	%esi
     2ac: 89 e8                        	movl	%ebp, %eax
     2ae: 45 0f 57 d2                  	xorps	%xmm10, %xmm10
     2b2: f2 4c 0f 2a d0               	cvtsi2sd	%rax, %xmm10
     2b7: 83 fd 02                     	cmpl	$0x2, %ebp
     2ba: 74 94                        	je	0x250 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x220>
     2bc: 41 89 f4                     	movl	%esi, %r12d
     2bf: 41 83 e4 fe                  	andl	$-0x2, %r12d
     2c3: 48 8d 7e ff                  	leaq	-0x1(%rsi), %rdi
     2c7: 45 31 ed                     	xorl	%r13d, %r13d
     2ca: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     2d0: 0f 57 f6                     	xorps	%xmm6, %xmm6
     2d3: f2 41 0f 2a f5               	cvtsi2sd	%r13d, %xmm6
     2d8: f2 41 0f 59 f1               	mulsd	%xmm9, %xmm6
     2dd: f2 41 0f 5e f2               	divsd	%xmm10, %xmm6
     2e2: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     2e6: e8 00 00 00 00               	callq	0x2eb <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x2bb>
     2eb: 42 8d 1c 2f                  	leal	(%rdi,%r13), %ebx
     2ef: f2 41 0f 11 04 de            	movsd	%xmm0, (%r14,%rbx,8)
     2f5: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     2f9: e8 00 00 00 00               	callq	0x2fe <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x2ce>
     2fe: f2 41 0f 11 04 df            	movsd	%xmm0, (%r15,%rbx,8)
     304: 41 8d 45 01                  	leal	0x1(%r13), %eax
     308: 0f 57 f6                     	xorps	%xmm6, %xmm6
     30b: f2 0f 2a f0                  	cvtsi2sd	%eax, %xmm6
     30f: f2 41 0f 59 f1               	mulsd	%xmm9, %xmm6
     314: f2 41 0f 5e f2               	divsd	%xmm10, %xmm6
     319: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     31d: e8 00 00 00 00               	callq	0x322 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x2f2>
     322: 42 8d 1c 2e                  	leal	(%rsi,%r13), %ebx
     326: f2 41 0f 11 04 de            	movsd	%xmm0, (%r14,%rbx,8)
     32c: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     330: e8 00 00 00 00               	callq	0x335 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x305>
     335: f2 41 0f 11 04 df            	movsd	%xmm0, (%r15,%rbx,8)
     33b: 49 83 c5 02                  	addq	$0x2, %r13
     33f: 4d 39 ec                     	cmpq	%r13, %r12
     342: 75 8c                        	jne	0x2d0 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x2a0>
     344: 40 f6 c6 01                  	testb	$0x1, %sil
     348: 48 8b 5c 24 38               	movq	0x38(%rsp), %rbx
     34d: 0f 85 00 ff ff ff            	jne	0x253 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x223>
     353: e9 42 ff ff ff               	jmp	0x29a <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x26a>
     358: 48 89 f7                     	movq	%rsi, %rdi
     35b: 44 89 f6                     	movl	%r14d, %esi
     35e: 83 e6 fe                     	andl	$-0x2, %esi
     361: 31 ed                        	xorl	%ebp, %ebp
     363: f2 44 0f 10 05 30 00 00 00   	movsd	0x30(%rip), %xmm8       # 0x39c <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x36c>
     36c: 0f 1f 40 00                  	nopl	(%rax)
     370: 0f 57 f6                     	xorps	%xmm6, %xmm6
     373: f2 0f 2a f5                  	cvtsi2sd	%ebp, %xmm6
     377: f2 41 0f 59 f0               	mulsd	%xmm8, %xmm6
     37c: f2 0f 5e f7                  	divsd	%xmm7, %xmm6
     380: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     384: e8 00 00 00 00               	callq	0x389 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x359>
     389: f2 41 0f 11 04 ef            	movsd	%xmm0, (%r15,%rbp,8)
     38f: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     393: e8 00 00 00 00               	callq	0x398 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x368>
     398: f2 41 0f 11 44 ed 00         	movsd	%xmm0, (%r13,%rbp,8)
     39f: 8d 45 01                     	leal	0x1(%rbp), %eax
     3a2: 0f 57 f6                     	xorps	%xmm6, %xmm6
     3a5: f2 0f 2a f0                  	cvtsi2sd	%eax, %xmm6
     3a9: f2 41 0f 59 f0               	mulsd	%xmm8, %xmm6
     3ae: f2 0f 5e f7                  	divsd	%xmm7, %xmm6
     3b2: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     3b6: e8 00 00 00 00               	callq	0x3bb <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x38b>
     3bb: f2 41 0f 11 44 ef 08         	movsd	%xmm0, 0x8(%r15,%rbp,8)
     3c2: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     3c6: e8 00 00 00 00               	callq	0x3cb <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x39b>
     3cb: f2 41 0f 11 44 ed 08         	movsd	%xmm0, 0x8(%r13,%rbp,8)
     3d2: 48 83 c5 02                  	addq	$0x2, %rbp
     3d6: 48 39 f5                     	cmpq	%rsi, %rbp
     3d9: 75 95                        	jne	0x370 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x340>
     3db: 41 f6 c6 01                  	testb	$0x1, %r14b
     3df: 48 89 fe                     	movq	%rdi, %rsi
     3e2: 74 4c                        	je	0x430 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x400>
     3e4: 66 48 0f 6e c5               	movq	%rbp, %xmm0
     3e9: 66 0f 62 05 10 00 00 00      	punpckldq	0x10(%rip), %xmm0 # xmm0 = xmm0[0],mem[0],xmm0[1],mem[1]
                                                                        # 0x401 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x3d1>
     3f1: 66 0f 5c 05 20 00 00 00      	subpd	0x20(%rip), %xmm0       # 0x419 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x3e9>
     3f9: 66 0f 28 f0                  	movapd	%xmm0, %xmm6
     3fd: 66 0f 15 f0                  	unpckhpd	%xmm0, %xmm6            # xmm6 = xmm6[1],xmm0[1]
     401: f2 0f 58 f0                  	addsd	%xmm0, %xmm6
     405: f2 0f 59 35 30 00 00 00      	mulsd	0x30(%rip), %xmm6       # 0x43d <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x40d>
     40d: f2 0f 5e f7                  	divsd	%xmm7, %xmm6
     411: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     415: e8 00 00 00 00               	callq	0x41a <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x3ea>
     41a: f2 41 0f 11 04 ef            	movsd	%xmm0, (%r15,%rbp,8)
     420: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     424: e8 00 00 00 00               	callq	0x429 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x3f9>
     429: f2 41 0f 11 44 ed 00         	movsd	%xmm0, (%r13,%rbp,8)
     430: 85 db                        	testl	%ebx, %ebx
     432: 74 25                        	je	0x459 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x429>
     434: 89 dd                        	movl	%ebx, %ebp
     436: 48 8b 44 24 28               	movq	0x28(%rsp), %rax
     43b: 0f 57 ff                     	xorps	%xmm7, %xmm7
     43e: f2 48 0f 2a f8               	cvtsi2sd	%rax, %xmm7
     443: c1 ed 02                     	shrl	$0x2, %ebp
     446: 4d 8b 2c 24                  	movq	(%r12), %r13
     44a: 4c 8b 3e                     	movq	(%rsi), %r15
     44d: 83 fb 01                     	cmpl	$0x1, %ebx
     450: 75 4d                        	jne	0x49f <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x46f>
     452: 31 db                        	xorl	%ebx, %ebx
     454: e9 ea 00 00 00               	jmp	0x543 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x513>
     459: 48 8b 4c 24 30               	movq	0x30(%rsp), %rcx
     45e: 48 81 c1 e8 00 00 00         	addq	$0xe8, %rcx
     465: 4c 89 f2                     	movq	%r14, %rdx
     468: 0f 28 74 24 40               	movaps	0x40(%rsp), %xmm6
     46d: 0f 28 7c 24 50               	movaps	0x50(%rsp), %xmm7
     472: 44 0f 28 44 24 60            	movaps	0x60(%rsp), %xmm8
     478: 44 0f 28 4c 24 70            	movaps	0x70(%rsp), %xmm9
     47e: 44 0f 28 94 24 80 00 00 00   	movaps	0x80(%rsp), %xmm10
     487: 48 81 c4 98 00 00 00         	addq	$0x98, %rsp
     48e: 5b                           	popq	%rbx
     48f: 5d                           	popq	%rbp
     490: 5f                           	popq	%rdi
     491: 5e                           	popq	%rsi
     492: 41 5c                        	popq	%r12
     494: 41 5d                        	popq	%r13
     496: 41 5e                        	popq	%r14
     498: 41 5f                        	popq	%r15
     49a: e9 00 00 00 00               	jmp	0x49f <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x46f>
     49f: 41 89 c4                     	movl	%eax, %r12d
     4a2: 41 83 e4 fe                  	andl	$-0x2, %r12d
     4a6: 31 db                        	xorl	%ebx, %ebx
     4a8: f2 44 0f 10 05 38 00 00 00   	movsd	0x38(%rip), %xmm8       # 0x4e9 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x4b9>
     4b1: f2 44 0f 10 0d 30 00 00 00   	movsd	0x30(%rip), %xmm9       # 0x4ea <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x4ba>
     4ba: 89 ee                        	movl	%ebp, %esi
     4bc: 0f 1f 40 00                  	nopl	(%rax)
     4c0: 89 f0                        	movl	%esi, %eax
     4c2: 0f 57 f6                     	xorps	%xmm6, %xmm6
     4c5: f2 48 0f 2a f0               	cvtsi2sd	%rax, %xmm6
     4ca: f2 41 0f 58 f0               	addsd	%xmm8, %xmm6
     4cf: f2 41 0f 59 f1               	mulsd	%xmm9, %xmm6
     4d4: f2 0f 5e f7                  	divsd	%xmm7, %xmm6
     4d8: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     4dc: e8 00 00 00 00               	callq	0x4e1 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x4b1>
     4e1: f2 41 0f 11 44 dd 00         	movsd	%xmm0, (%r13,%rbx,8)
     4e8: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     4ec: e8 00 00 00 00               	callq	0x4f1 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x4c1>
     4f1: 8d 46 01                     	leal	0x1(%rsi), %eax
     4f4: 0f 57 f6                     	xorps	%xmm6, %xmm6
     4f7: f2 48 0f 2a f0               	cvtsi2sd	%rax, %xmm6
     4fc: f2 41 0f 11 04 df            	movsd	%xmm0, (%r15,%rbx,8)
     502: f2 41 0f 58 f0               	addsd	%xmm8, %xmm6
     507: f2 41 0f 59 f1               	mulsd	%xmm9, %xmm6
     50c: f2 0f 5e f7                  	divsd	%xmm7, %xmm6
     510: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     514: e8 00 00 00 00               	callq	0x519 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x4e9>
     519: f2 41 0f 11 44 dd 08         	movsd	%xmm0, 0x8(%r13,%rbx,8)
     520: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     524: e8 00 00 00 00               	callq	0x529 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x4f9>
     529: f2 41 0f 11 44 df 08         	movsd	%xmm0, 0x8(%r15,%rbx,8)
     530: 48 83 c3 02                  	addq	$0x2, %rbx
     534: 83 c6 02                     	addl	$0x2, %esi
     537: 49 39 dc                     	cmpq	%rbx, %r12
     53a: 75 84                        	jne	0x4c0 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x490>
     53c: f6 44 24 28 01               	testb	$0x1, 0x28(%rsp)
     541: 74 3d                        	je	0x580 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x550>
     543: 01 dd                        	addl	%ebx, %ebp
     545: 0f 57 f6                     	xorps	%xmm6, %xmm6
     548: f2 48 0f 2a f5               	cvtsi2sd	%rbp, %xmm6
     54d: f2 0f 58 35 38 00 00 00      	addsd	0x38(%rip), %xmm6       # 0x58d <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x55d>
     555: f2 0f 59 35 30 00 00 00      	mulsd	0x30(%rip), %xmm6       # 0x58d <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x55d>
     55d: f2 0f 5e f7                  	divsd	%xmm7, %xmm6
     561: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     565: e8 00 00 00 00               	callq	0x56a <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x53a>
     56a: f2 41 0f 11 44 dd 00         	movsd	%xmm0, (%r13,%rbx,8)
     571: 66 0f 28 c6                  	movapd	%xmm6, %xmm0
     575: e8 00 00 00 00               	callq	0x57a <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x54a>
     57a: f2 41 0f 11 04 df            	movsd	%xmm0, (%r15,%rbx,8)
     580: 48 8b 74 24 30               	movq	0x30(%rsp), %rsi
     585: 48 81 c6 e8 00 00 00         	addq	$0xe8, %rsi
     58c: 48 89 f1                     	movq	%rsi, %rcx
     58f: 4c 89 f2                     	movq	%r14, %rdx
     592: e8 00 00 00 00               	callq	0x597 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x567>
     597: 4d 85 f6                     	testq	%r14, %r14
     59a: 0f 84 0b 01 00 00            	je	0x6ab <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x67b>
     5a0: 0f 57 f6                     	xorps	%xmm6, %xmm6
     5a3: f2 48 0f 2a 74 24 28         	cvtsi2sdq	0x28(%rsp), %xmm6
     5aa: 48 8b 36                     	movq	(%rsi), %rsi
     5ad: 41 83 fe 01                  	cmpl	$0x1, %r14d
     5b1: 75 07                        	jne	0x5ba <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x58a>
     5b3: 31 ff                        	xorl	%edi, %edi
     5b5: e9 9d 00 00 00               	jmp	0x657 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x627>
     5ba: 44 89 f3                     	movl	%r14d, %ebx
     5bd: 83 e3 fe                     	andl	$-0x2, %ebx
     5c0: 31 ff                        	xorl	%edi, %edi
     5c2: f2 0f 10 3d 38 00 00 00      	movsd	0x38(%rip), %xmm7       # 0x602 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x5d2>
     5ca: f2 44 0f 10 05 30 00 00 00   	movsd	0x30(%rip), %xmm8       # 0x603 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x5d3>
     5d3: f2 44 0f 10 0d 40 00 00 00   	movsd	0x40(%rip), %xmm9       # 0x61c <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x5ec>
     5dc: 0f 1f 40 00                  	nopl	(%rax)
     5e0: 0f 57 c0                     	xorps	%xmm0, %xmm0
     5e3: f2 0f 2a c7                  	cvtsi2sd	%edi, %xmm0
     5e7: f2 0f 58 c7                  	addsd	%xmm7, %xmm0
     5eb: f2 41 0f 59 c0               	mulsd	%xmm8, %xmm0
     5f0: f2 0f 5e c6                  	divsd	%xmm6, %xmm0
     5f4: e8 00 00 00 00               	callq	0x5f9 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x5c9>
     5f9: 66 0f 28 c8                  	movapd	%xmm0, %xmm1
     5fd: f2 41 0f 59 c9               	mulsd	%xmm9, %xmm1
     602: f2 0f 59 c1                  	mulsd	%xmm1, %xmm0
     606: e8 00 00 00 00               	callq	0x60b <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x5db>
     60b: 8d 47 01                     	leal	0x1(%rdi), %eax
     60e: 0f 57 c9                     	xorps	%xmm1, %xmm1
     611: f2 0f 2a c8                  	cvtsi2sd	%eax, %xmm1
     615: f2 0f 11 04 fe               	movsd	%xmm0, (%rsi,%rdi,8)
     61a: f2 0f 58 cf                  	addsd	%xmm7, %xmm1
     61e: f2 41 0f 59 c8               	mulsd	%xmm8, %xmm1
     623: f2 0f 5e ce                  	divsd	%xmm6, %xmm1
     627: 66 0f 28 c1                  	movapd	%xmm1, %xmm0
     62b: e8 00 00 00 00               	callq	0x630 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x600>
     630: 66 0f 28 c8                  	movapd	%xmm0, %xmm1
     634: f2 41 0f 59 c9               	mulsd	%xmm9, %xmm1
     639: f2 0f 59 c1                  	mulsd	%xmm1, %xmm0
     63d: e8 00 00 00 00               	callq	0x642 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x612>
     642: f2 0f 11 44 fe 08            	movsd	%xmm0, 0x8(%rsi,%rdi,8)
     648: 48 83 c7 02                  	addq	$0x2, %rdi
     64c: 48 39 df                     	cmpq	%rbx, %rdi
     64f: 75 8f                        	jne	0x5e0 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x5b0>
     651: 41 f6 c6 01                  	testb	$0x1, %r14b
     655: 74 54                        	je	0x6ab <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x67b>
     657: 66 48 0f 6e cf               	movq	%rdi, %xmm1
     65c: 66 0f 62 0d 10 00 00 00      	punpckldq	0x10(%rip), %xmm1 # xmm1 = xmm1[0],mem[0],xmm1[1],mem[1]
                                                                        # 0x674 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x644>
     664: 66 0f 5c 0d 20 00 00 00      	subpd	0x20(%rip), %xmm1       # 0x68c <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x65c>
     66c: 66 0f 28 c1                  	movapd	%xmm1, %xmm0
     670: 66 0f 15 c1                  	unpckhpd	%xmm1, %xmm0            # xmm0 = xmm0[1],xmm1[1]
     674: f2 0f 58 c1                  	addsd	%xmm1, %xmm0
     678: f2 0f 58 05 38 00 00 00      	addsd	0x38(%rip), %xmm0       # 0x6b8 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x688>
     680: f2 0f 59 05 30 00 00 00      	mulsd	0x30(%rip), %xmm0       # 0x6b8 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x688>
     688: f2 0f 5e c6                  	divsd	%xmm6, %xmm0
     68c: e8 00 00 00 00               	callq	0x691 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x661>
     691: f2 0f 10 0d 40 00 00 00      	movsd	0x40(%rip), %xmm1       # 0x6d9 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x6a9>
     699: f2 0f 59 c8                  	mulsd	%xmm0, %xmm1
     69d: f2 0f 59 c1                  	mulsd	%xmm1, %xmm0
     6a1: e8 00 00 00 00               	callq	0x6a6 <stx_vorbis::detail::prepare_transform(stx_vorbis::detail::Transform&, unsigned int)+0x676>
     6a6: f2 0f 11 04 fe               	movsd	%xmm0, (%rsi,%rdi,8)
     6ab: 0f 28 74 24 40               	movaps	0x40(%rsp), %xmm6
     6b0: 0f 28 7c 24 50               	movaps	0x50(%rsp), %xmm7
     6b5: 44 0f 28 44 24 60            	movaps	0x60(%rsp), %xmm8
     6bb: 44 0f 28 4c 24 70            	movaps	0x70(%rsp), %xmm9
     6c1: 44 0f 28 94 24 80 00 00 00   	movaps	0x80(%rsp), %xmm10
     6ca: 48 81 c4 98 00 00 00         	addq	$0x98, %rsp
     6d1: 5b                           	popq	%rbx
     6d2: 5d                           	popq	%rbp
     6d3: 5f                           	popq	%rdi
     6d4: 5e                           	popq	%rsi
     6d5: 41 5c                        	popq	%r12
     6d7: 41 5d                        	popq	%r13
     6d9: 41 5e                        	popq	%r14
     6db: 41 5f                        	popq	%r15
     6dd: c3                           	retq
     6de: 66 90                        	nop

00000000000006e0 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)>:
     6e0: 41 57                        	pushq	%r15
     6e2: 41 56                        	pushq	%r14
     6e4: 41 55                        	pushq	%r13
     6e6: 41 54                        	pushq	%r12
     6e8: 56                           	pushq	%rsi
     6e9: 57                           	pushq	%rdi
     6ea: 55                           	pushq	%rbp
     6eb: 53                           	pushq	%rbx
     6ec: 48 83 ec 68                  	subq	$0x68, %rsp
     6f0: 48 89 54 24 20               	movq	%rdx, 0x20(%rsp)
     6f5: 48 89 4c 24 18               	movq	%rcx, 0x18(%rsp)
     6fa: 48 83 bc 24 d8 00 00 00 00   	cmpq	$0x0, 0xd8(%rsp)
     703: 0f 84 c8 03 00 00            	je	0xad1 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x3f1>
     709: 48 83 bc 24 d0 00 00 00 00   	cmpq	$0x0, 0xd0(%rsp)
     712: 0f 84 b9 03 00 00            	je	0xad1 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x3f1>
     718: 48 8b 84 24 d0 00 00 00      	movq	0xd0(%rsp), %rax
     720: 48 8d 0c 00                  	leaq	(%rax,%rax), %rcx
     724: 48 89 4c 24 50               	movq	%rcx, 0x50(%rsp)
     729: 48 8d 0c c5 00 00 00 00      	leaq	(,%rax,8), %rcx
     731: 48 89 4c 24 48               	movq	%rcx, 0x48(%rsp)
     736: 48 89 c1                     	movq	%rax, %rcx
     739: 48 c1 e1 04                  	shlq	$0x4, %rcx
     73d: 48 89 4c 24 28               	movq	%rcx, 0x28(%rsp)
     742: 49 8d 0c c0                  	leaq	(%r8,%rax,8), %rcx
     746: 48 89 4c 24 40               	movq	%rcx, 0x40(%rsp)
     74b: 49 8d 0c c1                  	leaq	(%r9,%rax,8), %rcx
     74f: 48 89 4c 24 38               	movq	%rcx, 0x38(%rsp)
     754: 49 89 c2                     	movq	%rax, %r10
     757: 49 83 e2 fe                  	andq	$-0x2, %r10
     75b: 48 8b 54 24 20               	movq	0x20(%rsp), %rdx
     760: 4c 8d 24 c2                  	leaq	(%rdx,%rax,8), %r12
     764: 48 8b 4c 24 18               	movq	0x18(%rsp), %rcx
     769: 4c 8d 2c c1                  	leaq	(%rcx,%rax,8), %r13
     76d: 31 ff                        	xorl	%edi, %edi
     76f: 48 89 44 24 30               	movq	%rax, 0x30(%rsp)
     774: 31 f6                        	xorl	%esi, %esi
     776: eb 41                        	jmp	0x7b9 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xd9>
     778: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
     780: 48 8b 44 24 50               	movq	0x50(%rsp), %rax
     785: 48 8b 74 24 58               	movq	0x58(%rsp), %rsi
     78a: 48 01 c6                     	addq	%rax, %rsi
     78d: 48 8b 7c 24 60               	movq	0x60(%rsp), %rdi
     792: 48 ff c7                     	incq	%rdi
     795: 4c 8b 5c 24 28               	movq	0x28(%rsp), %r11
     79a: 4c 01 da                     	addq	%r11, %rdx
     79d: 4c 01 d9                     	addq	%r11, %rcx
     7a0: 4d 01 dc                     	addq	%r11, %r12
     7a3: 4d 01 dd                     	addq	%r11, %r13
     7a6: 48 01 44 24 30               	addq	%rax, 0x30(%rsp)
     7ab: 48 3b b4 24 d8 00 00 00      	cmpq	0xd8(%rsp), %rsi
     7b3: 0f 83 18 03 00 00            	jae	0xad1 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x3f1>
     7b9: 48 89 74 24 58               	movq	%rsi, 0x58(%rsp)
     7be: 48 83 bc 24 d0 00 00 00 08   	cmpq	$0x8, 0xd0(%rsp)
     7c7: 48 89 7c 24 60               	movq	%rdi, 0x60(%rsp)
     7cc: 0f 83 be 00 00 00            	jae	0x890 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x1b0>
     7d2: 45 31 ff                     	xorl	%r15d, %r15d
     7d5: 48 8b b4 24 d0 00 00 00      	movq	0xd0(%rsp), %rsi
     7dd: 4c 29 fe                     	subq	%r15, %rsi
     7e0: 4b 8d 04 f9                  	leaq	(%r9,%r15,8), %rax
     7e4: 4f 8d 1c f8                  	leaq	(%r8,%r15,8), %r11
     7e8: 4a 8d 3c fa                  	leaq	(%rdx,%r15,8), %rdi
     7ec: 4e 8d 34 f9                  	leaq	(%rcx,%r15,8), %r14
     7f0: 4c 03 7c 24 30               	addq	0x30(%rsp), %r15
     7f5: 48 8b 5c 24 20               	movq	0x20(%rsp), %rbx
     7fa: 4a 8d 2c fb                  	leaq	(%rbx,%r15,8), %rbp
     7fe: 48 8b 5c 24 18               	movq	0x18(%rsp), %rbx
     803: 4e 8d 3c fb                  	leaq	(%rbx,%r15,8), %r15
     807: 31 db                        	xorl	%ebx, %ebx
     809: 0f 1f 80 00 00 00 00         	nopl	(%rax)
     810: f2 41 0f 10 04 db            	movsd	(%r11,%rbx,8), %xmm0
     816: f2 0f 10 0c d8               	movsd	(%rax,%rbx,8), %xmm1
     81b: f2 41 0f 10 14 df            	movsd	(%r15,%rbx,8), %xmm2
     821: 66 0f 28 d8                  	movapd	%xmm0, %xmm3
     825: f2 0f 59 da                  	mulsd	%xmm2, %xmm3
     829: f2 0f 10 64 dd 00            	movsd	(%rbp,%rbx,8), %xmm4
     82f: 66 0f 28 e9                  	movapd	%xmm1, %xmm5
     833: f2 0f 59 ec                  	mulsd	%xmm4, %xmm5
     837: f2 0f 5c dd                  	subsd	%xmm5, %xmm3
     83b: f2 0f 59 ca                  	mulsd	%xmm2, %xmm1
     83f: f2 0f 59 c4                  	mulsd	%xmm4, %xmm0
     843: f2 0f 58 c1                  	addsd	%xmm1, %xmm0
     847: f2 41 0f 10 0c de            	movsd	(%r14,%rbx,8), %xmm1
     84d: f2 0f 10 14 df               	movsd	(%rdi,%rbx,8), %xmm2
     852: 66 0f 28 e1                  	movapd	%xmm1, %xmm4
     856: f2 0f 58 e3                  	addsd	%xmm3, %xmm4
     85a: f2 41 0f 11 24 de            	movsd	%xmm4, (%r14,%rbx,8)
     860: 66 0f 28 e2                  	movapd	%xmm2, %xmm4
     864: f2 0f 58 e0                  	addsd	%xmm0, %xmm4
     868: f2 0f 11 24 df               	movsd	%xmm4, (%rdi,%rbx,8)
     86d: f2 0f 5c cb                  	subsd	%xmm3, %xmm1
     871: f2 41 0f 11 0c df            	movsd	%xmm1, (%r15,%rbx,8)
     877: f2 0f 5c d0                  	subsd	%xmm0, %xmm2
     87b: f2 0f 11 54 dd 00            	movsd	%xmm2, (%rbp,%rbx,8)
     881: 48 ff c3                     	incq	%rbx
     884: 48 39 de                     	cmpq	%rbx, %rsi
     887: 75 87                        	jne	0x810 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x130>
     889: e9 f2 fe ff ff               	jmp	0x780 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xa0>
     88e: 66 90                        	nop
     890: 48 8b 74 24 28               	movq	0x28(%rsp), %rsi
     895: 48 89 f0                     	movq	%rsi, %rax
     898: 48 0f af c7                  	imulq	%rdi, %rax
     89c: 48 8b 6c 24 18               	movq	0x18(%rsp), %rbp
     8a1: 4c 8d 3c 28                  	leaq	(%rax,%rbp), %r15
     8a5: 4c 8b 5c 24 20               	movq	0x20(%rsp), %r11
     8aa: 4d 8d 34 03                  	leaq	(%r11,%rax), %r14
     8ae: 48 8d 1c 06                  	leaq	(%rsi,%rax), %rbx
     8b2: 49 8d 3c 1b                  	leaq	(%r11,%rbx), %rdi
     8b6: 48 03 44 24 48               	addq	0x48(%rsp), %rax
     8bb: 49 8d 34 03                  	leaq	(%r11,%rax), %rsi
     8bf: 48 01 eb                     	addq	%rbp, %rbx
     8c2: 48 01 e8                     	addq	%rbp, %rax
     8c5: 48 39 f8                     	cmpq	%rdi, %rax
     8c8: 0f 92 44 24 17               	setb	0x17(%rsp)
     8cd: 48 39 de                     	cmpq	%rbx, %rsi
     8d0: 0f 92 44 24 16               	setb	0x16(%rsp)
     8d5: 48 39 f0                     	cmpq	%rsi, %rax
     8d8: 0f 92 44 24 15               	setb	0x15(%rsp)
     8dd: 49 39 de                     	cmpq	%rbx, %r14
     8e0: 0f 92 44 24 14               	setb	0x14(%rsp)
     8e5: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
     8ea: 48 39 e8                     	cmpq	%rbp, %rax
     8ed: 0f 92 44 24 13               	setb	0x13(%rsp)
     8f2: 49 39 d8                     	cmpq	%rbx, %r8
     8f5: 0f 92 44 24 12               	setb	0x12(%rsp)
     8fa: 4c 8b 5c 24 38               	movq	0x38(%rsp), %r11
     8ff: 4c 39 d8                     	cmpq	%r11, %rax
     902: 0f 92 44 24 11               	setb	0x11(%rsp)
     907: 49 39 d9                     	cmpq	%rbx, %r9
     90a: 0f 92 44 24 10               	setb	0x10(%rsp)
     90f: 49 39 ff                     	cmpq	%rdi, %r15
     912: 0f 92 c3                     	setb	%bl
     915: 48 39 c6                     	cmpq	%rax, %rsi
     918: 0f 92 44 24 0f               	setb	0xf(%rsp)
     91d: 49 39 f7                     	cmpq	%rsi, %r15
     920: 0f 92 44 24 0e               	setb	0xe(%rsp)
     925: 49 39 c6                     	cmpq	%rax, %r14
     928: 0f 92 44 24 0d               	setb	0xd(%rsp)
     92d: 49 39 ef                     	cmpq	%rbp, %r15
     930: 0f 92 44 24 0c               	setb	0xc(%rsp)
     935: 49 39 c0                     	cmpq	%rax, %r8
     938: 0f 92 44 24 0b               	setb	0xb(%rsp)
     93d: 4d 39 df                     	cmpq	%r11, %r15
     940: 41 0f 92 c7                  	setb	%r15b
     944: 49 39 c1                     	cmpq	%rax, %r9
     947: 0f 92 44 24 0a               	setb	0xa(%rsp)
     94c: 48 39 ee                     	cmpq	%rbp, %rsi
     94f: 0f 92 c0                     	setb	%al
     952: 49 39 f8                     	cmpq	%rdi, %r8
     955: 0f 92 44 24 09               	setb	0x9(%rsp)
     95a: 4c 39 de                     	cmpq	%r11, %rsi
     95d: 0f 92 44 24 08               	setb	0x8(%rsp)
     962: 49 39 f9                     	cmpq	%rdi, %r9
     965: 40 0f 92 c7                  	setb	%dil
     969: 49 39 ee                     	cmpq	%rbp, %r14
     96c: 40 0f 92 c5                  	setb	%bpl
     970: 49 39 f0                     	cmpq	%rsi, %r8
     973: 0f 92 44 24 07               	setb	0x7(%rsp)
     978: 4d 39 de                     	cmpq	%r11, %r14
     97b: 41 0f 92 c6                  	setb	%r14b
     97f: 49 39 f1                     	cmpq	%rsi, %r9
     982: 40 0f 92 c6                  	setb	%sil
     986: 44 0f b6 5c 24 16            	movzbl	0x16(%rsp), %r11d
     98c: 44 84 5c 24 17               	testb	%r11b, 0x17(%rsp)
     991: 0f 85 3b fe ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     997: 44 0f b6 5c 24 14            	movzbl	0x14(%rsp), %r11d
     99d: 44 20 5c 24 15               	andb	%r11b, 0x15(%rsp)
     9a2: 0f 85 2a fe ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     9a8: 44 0f b6 5c 24 12            	movzbl	0x12(%rsp), %r11d
     9ae: 44 20 5c 24 13               	andb	%r11b, 0x13(%rsp)
     9b3: 0f 85 19 fe ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     9b9: 44 0f b6 5c 24 10            	movzbl	0x10(%rsp), %r11d
     9bf: 44 20 5c 24 11               	andb	%r11b, 0x11(%rsp)
     9c4: 0f 85 08 fe ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     9ca: 22 5c 24 0f                  	andb	0xf(%rsp), %bl
     9ce: 0f 85 fe fd ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     9d4: 44 0f b6 5c 24 0d            	movzbl	0xd(%rsp), %r11d
     9da: 44 20 5c 24 0e               	andb	%r11b, 0xe(%rsp)
     9df: 0f 85 ed fd ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     9e5: 44 0f b6 5c 24 0b            	movzbl	0xb(%rsp), %r11d
     9eb: 44 20 5c 24 0c               	andb	%r11b, 0xc(%rsp)
     9f0: 0f 85 dc fd ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     9f6: 44 22 7c 24 0a               	andb	0xa(%rsp), %r15b
     9fb: 0f 85 d1 fd ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     a01: 22 44 24 09                  	andb	0x9(%rsp), %al
     a05: 0f 85 c7 fd ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     a0b: 40 20 7c 24 08               	andb	%dil, 0x8(%rsp)
     a10: 0f 85 bc fd ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     a16: 40 22 6c 24 07               	andb	0x7(%rsp), %bpl
     a1b: 0f 85 b1 fd ff ff            	jne	0x7d2 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf2>
     a21: 41 bf 00 00 00 00            	movl	$0x0, %r15d
     a27: 41 20 f6                     	andb	%sil, %r14b
     a2a: 0f 85 a5 fd ff ff            	jne	0x7d5 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf5>
     a30: 31 c0                        	xorl	%eax, %eax
     a32: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
     a40: 66 41 0f 10 04 c0            	movupd	(%r8,%rax,8), %xmm0
     a46: 66 41 0f 10 0c c1            	movupd	(%r9,%rax,8), %xmm1
     a4c: 66 41 0f 10 54 c5 00         	movupd	(%r13,%rax,8), %xmm2
     a53: 66 0f 28 d8                  	movapd	%xmm0, %xmm3
     a57: 66 0f 59 da                  	mulpd	%xmm2, %xmm3
     a5b: 66 41 0f 10 24 c4            	movupd	(%r12,%rax,8), %xmm4
     a61: 66 0f 28 e9                  	movapd	%xmm1, %xmm5
     a65: 66 0f 59 ec                  	mulpd	%xmm4, %xmm5
     a69: 66 0f 5c dd                  	subpd	%xmm5, %xmm3
     a6d: 66 0f 59 ca                  	mulpd	%xmm2, %xmm1
     a71: 66 0f 59 c4                  	mulpd	%xmm4, %xmm0
     a75: 66 0f 58 c1                  	addpd	%xmm1, %xmm0
     a79: 66 0f 10 0c c1               	movupd	(%rcx,%rax,8), %xmm1
     a7e: 66 0f 10 14 c2               	movupd	(%rdx,%rax,8), %xmm2
     a83: 66 0f 28 e1                  	movapd	%xmm1, %xmm4
     a87: 66 0f 58 e3                  	addpd	%xmm3, %xmm4
     a8b: 66 0f 11 24 c1               	movupd	%xmm4, (%rcx,%rax,8)
     a90: 66 0f 28 e2                  	movapd	%xmm2, %xmm4
     a94: 66 0f 58 e0                  	addpd	%xmm0, %xmm4
     a98: 66 0f 11 24 c2               	movupd	%xmm4, (%rdx,%rax,8)
     a9d: 66 0f 5c cb                  	subpd	%xmm3, %xmm1
     aa1: 66 41 0f 11 4c c5 00         	movupd	%xmm1, (%r13,%rax,8)
     aa8: 66 0f 5c d0                  	subpd	%xmm0, %xmm2
     aac: 66 41 0f 11 14 c4            	movupd	%xmm2, (%r12,%rax,8)
     ab2: 48 83 c0 02                  	addq	$0x2, %rax
     ab6: 49 39 c2                     	cmpq	%rax, %r10
     ab9: 75 85                        	jne	0xa40 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x360>
     abb: 4d 89 d7                     	movq	%r10, %r15
     abe: 4c 39 94 24 d0 00 00 00      	cmpq	%r10, 0xd0(%rsp)
     ac6: 0f 84 b4 fc ff ff            	je	0x780 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xa0>
     acc: e9 04 fd ff ff               	jmp	0x7d5 <stx_vorbis::detail::scalar_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf5>
     ad1: 48 83 c4 68                  	addq	$0x68, %rsp
     ad5: 5b                           	popq	%rbx
     ad6: 5d                           	popq	%rbp
     ad7: 5f                           	popq	%rdi
     ad8: 5e                           	popq	%rsi
     ad9: 41 5c                        	popq	%r12
     adb: 41 5d                        	popq	%r13
     add: 41 5e                        	popq	%r14
     adf: 41 5f                        	popq	%r15
     ae1: c3                           	retq
     ae2: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)

0000000000000af0 <stx_vorbis::detail::sse2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)>:
     af0: 41 57                        	pushq	%r15
     af2: 41 56                        	pushq	%r14
     af4: 56                           	pushq	%rsi
     af5: 57                           	pushq	%rdi
     af6: 53                           	pushq	%rbx
     af7: 48 83 ec 20                  	subq	$0x20, %rsp
     afb: 66 0f 29 7c 24 10            	movapd	%xmm7, 0x10(%rsp)
     b01: 66 0f 29 34 24               	movapd	%xmm6, (%rsp)
     b06: 48 8b 44 24 78               	movq	0x78(%rsp), %rax
     b0b: 4c 8b 54 24 70               	movq	0x70(%rsp), %r10
     b10: 49 83 fa 02                  	cmpq	$0x2, %r10
     b14: 0f 83 a9 00 00 00            	jae	0xbc3 <stx_vorbis::detail::sse2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xd3>
     b1a: 48 85 c0                     	testq	%rax, %rax
     b1d: 0f 84 72 01 00 00            	je	0xc95 <stx_vorbis::detail::sse2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x1a5>
     b23: 4d 85 d2                     	testq	%r10, %r10
     b26: 0f 84 69 01 00 00            	je	0xc95 <stx_vorbis::detail::sse2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x1a5>
     b2c: 4d 01 d2                     	addq	%r10, %r10
     b2f: 45 31 db                     	xorl	%r11d, %r11d
     b32: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
     b40: f2 41 0f 10 00               	movsd	(%r8), %xmm0
     b45: f2 41 0f 10 09               	movsd	(%r9), %xmm1
     b4a: f2 42 0f 10 14 d9            	movsd	(%rcx,%r11,8), %xmm2
     b50: f2 42 0f 10 5c d9 08         	movsd	0x8(%rcx,%r11,8), %xmm3
     b57: 66 0f 28 e0                  	movapd	%xmm0, %xmm4
     b5b: f2 0f 59 e3                  	mulsd	%xmm3, %xmm4
     b5f: f2 42 0f 10 2c da            	movsd	(%rdx,%r11,8), %xmm5
     b65: f2 42 0f 10 74 da 08         	movsd	0x8(%rdx,%r11,8), %xmm6
     b6c: 66 0f 28 f9                  	movapd	%xmm1, %xmm7
     b70: f2 0f 59 fe                  	mulsd	%xmm6, %xmm7
     b74: f2 0f 5c e7                  	subsd	%xmm7, %xmm4
     b78: f2 0f 59 cb                  	mulsd	%xmm3, %xmm1
     b7c: f2 0f 59 c6                  	mulsd	%xmm6, %xmm0
     b80: f2 0f 58 c1                  	addsd	%xmm1, %xmm0
     b84: 66 0f 28 ca                  	movapd	%xmm2, %xmm1
     b88: f2 0f 58 cc                  	addsd	%xmm4, %xmm1
     b8c: f2 42 0f 11 0c d9            	movsd	%xmm1, (%rcx,%r11,8)
     b92: 66 0f 28 cd                  	movapd	%xmm5, %xmm1
     b96: f2 0f 58 c8                  	addsd	%xmm0, %xmm1
     b9a: f2 42 0f 11 0c da            	movsd	%xmm1, (%rdx,%r11,8)
     ba0: f2 0f 5c d4                  	subsd	%xmm4, %xmm2
     ba4: f2 42 0f 11 54 d9 08         	movsd	%xmm2, 0x8(%rcx,%r11,8)
     bab: f2 0f 5c e8                  	subsd	%xmm0, %xmm5
     baf: f2 42 0f 11 6c da 08         	movsd	%xmm5, 0x8(%rdx,%r11,8)
     bb6: 4d 01 d3                     	addq	%r10, %r11
     bb9: 49 39 c3                     	cmpq	%rax, %r11
     bbc: 72 82                        	jb	0xb40 <stx_vorbis::detail::sse2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x50>
     bbe: e9 d2 00 00 00               	jmp	0xc95 <stx_vorbis::detail::sse2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x1a5>
     bc3: 48 85 c0                     	testq	%rax, %rax
     bc6: 0f 84 c9 00 00 00            	je	0xc95 <stx_vorbis::detail::sse2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x1a5>
     bcc: 4f 8d 1c 12                  	leaq	(%r10,%r10), %r11
     bd0: 4a 8d 34 d1                  	leaq	(%rcx,%r10,8), %rsi
     bd4: 4c 89 d7                     	movq	%r10, %rdi
     bd7: 48 c1 e7 04                  	shlq	$0x4, %rdi
     bdb: 4a 8d 1c d2                  	leaq	(%rdx,%r10,8), %rbx
     bdf: 45 31 f6                     	xorl	%r14d, %r14d
     be2: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
     bf0: 45 31 ff                     	xorl	%r15d, %r15d
     bf3: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
     c00: 66 43 0f 10 04 f8            	movupd	(%r8,%r15,8), %xmm0
     c06: 66 43 0f 10 0c f9            	movupd	(%r9,%r15,8), %xmm1
     c0c: 66 42 0f 10 14 fe            	movupd	(%rsi,%r15,8), %xmm2
     c12: 66 42 0f 10 1c fb            	movupd	(%rbx,%r15,8), %xmm3
     c18: 66 0f 28 e0                  	movapd	%xmm0, %xmm4
     c1c: 66 0f 59 e2                  	mulpd	%xmm2, %xmm4
     c20: 66 0f 28 e9                  	movapd	%xmm1, %xmm5
     c24: 66 0f 59 eb                  	mulpd	%xmm3, %xmm5
     c28: 66 0f 5c e5                  	subpd	%xmm5, %xmm4
     c2c: 66 0f 59 ca                  	mulpd	%xmm2, %xmm1
     c30: 66 0f 59 c3                  	mulpd	%xmm3, %xmm0
     c34: 66 0f 58 c1                  	addpd	%xmm1, %xmm0
     c38: 66 42 0f 10 0c f9            	movupd	(%rcx,%r15,8), %xmm1
     c3e: 66 42 0f 10 14 fa            	movupd	(%rdx,%r15,8), %xmm2
     c44: 66 0f 28 d9                  	movapd	%xmm1, %xmm3
     c48: 66 0f 58 dc                  	addpd	%xmm4, %xmm3
     c4c: 66 42 0f 11 1c f9            	movupd	%xmm3, (%rcx,%r15,8)
     c52: 66 0f 28 da                  	movapd	%xmm2, %xmm3
     c56: 66 0f 58 d8                  	addpd	%xmm0, %xmm3
     c5a: 66 42 0f 11 1c fa            	movupd	%xmm3, (%rdx,%r15,8)
     c60: 66 0f 5c cc                  	subpd	%xmm4, %xmm1
     c64: 66 42 0f 11 0c fe            	movupd	%xmm1, (%rsi,%r15,8)
     c6a: 66 0f 5c d0                  	subpd	%xmm0, %xmm2
     c6e: 66 42 0f 11 14 fb            	movupd	%xmm2, (%rbx,%r15,8)
     c74: 49 83 c7 02                  	addq	$0x2, %r15
     c78: 4d 39 d7                     	cmpq	%r10, %r15
     c7b: 72 83                        	jb	0xc00 <stx_vorbis::detail::sse2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x110>
     c7d: 4d 01 de                     	addq	%r11, %r14
     c80: 48 01 fe                     	addq	%rdi, %rsi
     c83: 48 01 fb                     	addq	%rdi, %rbx
     c86: 48 01 f9                     	addq	%rdi, %rcx
     c89: 48 01 fa                     	addq	%rdi, %rdx
     c8c: 49 39 c6                     	cmpq	%rax, %r14
     c8f: 0f 82 5b ff ff ff            	jb	0xbf0 <stx_vorbis::detail::sse2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x100>
     c95: 0f 28 34 24                  	movaps	(%rsp), %xmm6
     c99: 0f 28 7c 24 10               	movaps	0x10(%rsp), %xmm7
     c9e: 48 83 c4 20                  	addq	$0x20, %rsp
     ca2: 5b                           	popq	%rbx
     ca3: 5f                           	popq	%rdi
     ca4: 5e                           	popq	%rsi
     ca5: 41 5e                        	popq	%r14
     ca7: 41 5f                        	popq	%r15
     ca9: c3                           	retq
     caa: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)

0000000000000cb0 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)>:
     cb0: 41 57                        	pushq	%r15
     cb2: 41 56                        	pushq	%r14
     cb4: 56                           	pushq	%rsi
     cb5: 57                           	pushq	%rdi
     cb6: 53                           	pushq	%rbx
     cb7: 48 83 ec 20                  	subq	$0x20, %rsp
     cbb: c5 f9 29 7c 24 10            	vmovapd	%xmm7, 0x10(%rsp)
     cc1: c5 f9 29 34 24               	vmovapd	%xmm6, (%rsp)
     cc6: 48 8b 44 24 78               	movq	0x78(%rsp), %rax
     ccb: 4c 8b 54 24 70               	movq	0x70(%rsp), %r10
     cd0: 49 83 fa 04                  	cmpq	$0x4, %r10
     cd4: 0f 83 99 00 00 00            	jae	0xd73 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xc3>
     cda: 49 83 fa 02                  	cmpq	$0x2, %r10
     cde: 0f 83 56 01 00 00            	jae	0xe3a <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x18a>
     ce4: 48 85 c0                     	testq	%rax, %rax
     ce7: 0f 84 f0 01 00 00            	je	0xedd <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x22d>
     ced: 4d 85 d2                     	testq	%r10, %r10
     cf0: 0f 84 e7 01 00 00            	je	0xedd <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x22d>
     cf6: 4d 01 d2                     	addq	%r10, %r10
     cf9: 45 31 db                     	xorl	%r11d, %r11d
     cfc: 0f 1f 40 00                  	nopl	(%rax)
     d00: c4 c1 7b 10 00               	vmovsd	(%r8), %xmm0
     d05: c4 c1 7b 10 09               	vmovsd	(%r9), %xmm1
     d0a: c4 a1 7b 10 14 d9            	vmovsd	(%rcx,%r11,8), %xmm2
     d10: c4 a1 7b 10 5c d9 08         	vmovsd	0x8(%rcx,%r11,8), %xmm3
     d17: c5 fb 59 e3                  	vmulsd	%xmm3, %xmm0, %xmm4
     d1b: c4 a1 7b 10 2c da            	vmovsd	(%rdx,%r11,8), %xmm5
     d21: c4 a1 7b 10 74 da 08         	vmovsd	0x8(%rdx,%r11,8), %xmm6
     d28: c5 f3 59 fe                  	vmulsd	%xmm6, %xmm1, %xmm7
     d2c: c5 db 5c e7                  	vsubsd	%xmm7, %xmm4, %xmm4
     d30: c5 f3 59 cb                  	vmulsd	%xmm3, %xmm1, %xmm1
     d34: c5 fb 59 c6                  	vmulsd	%xmm6, %xmm0, %xmm0
     d38: c5 f3 58 c0                  	vaddsd	%xmm0, %xmm1, %xmm0
     d3c: c5 eb 58 cc                  	vaddsd	%xmm4, %xmm2, %xmm1
     d40: c4 a1 7b 11 0c d9            	vmovsd	%xmm1, (%rcx,%r11,8)
     d46: c5 d3 58 c8                  	vaddsd	%xmm0, %xmm5, %xmm1
     d4a: c4 a1 7b 11 0c da            	vmovsd	%xmm1, (%rdx,%r11,8)
     d50: c5 eb 5c cc                  	vsubsd	%xmm4, %xmm2, %xmm1
     d54: c4 a1 7b 11 4c d9 08         	vmovsd	%xmm1, 0x8(%rcx,%r11,8)
     d5b: c5 d3 5c c0                  	vsubsd	%xmm0, %xmm5, %xmm0
     d5f: c4 a1 7b 11 44 da 08         	vmovsd	%xmm0, 0x8(%rdx,%r11,8)
     d66: 4d 01 d3                     	addq	%r10, %r11
     d69: 49 39 c3                     	cmpq	%rax, %r11
     d6c: 72 92                        	jb	0xd00 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x50>
     d6e: e9 6a 01 00 00               	jmp	0xedd <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x22d>
     d73: 48 85 c0                     	testq	%rax, %rax
     d76: 0f 84 61 01 00 00            	je	0xedd <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x22d>
     d7c: 4f 8d 1c 12                  	leaq	(%r10,%r10), %r11
     d80: 4a 8d 34 d1                  	leaq	(%rcx,%r10,8), %rsi
     d84: 4c 89 d7                     	movq	%r10, %rdi
     d87: 48 c1 e7 04                  	shlq	$0x4, %rdi
     d8b: 4a 8d 1c d2                  	leaq	(%rdx,%r10,8), %rbx
     d8f: 45 31 f6                     	xorl	%r14d, %r14d
     d92: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
     da0: 45 31 ff                     	xorl	%r15d, %r15d
     da3: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
     db0: c4 81 7d 10 04 f8            	vmovupd	(%r8,%r15,8), %ymm0
     db6: c4 81 7d 10 0c f9            	vmovupd	(%r9,%r15,8), %ymm1
     dbc: c4 a1 7d 10 14 fe            	vmovupd	(%rsi,%r15,8), %ymm2
     dc2: c4 a1 7d 10 1c fb            	vmovupd	(%rbx,%r15,8), %ymm3
     dc8: c5 fd 59 e2                  	vmulpd	%ymm2, %ymm0, %ymm4
     dcc: c5 f5 59 eb                  	vmulpd	%ymm3, %ymm1, %ymm5
     dd0: c5 dd 5c e5                  	vsubpd	%ymm5, %ymm4, %ymm4
     dd4: c5 f5 59 ca                  	vmulpd	%ymm2, %ymm1, %ymm1
     dd8: c5 fd 59 c3                  	vmulpd	%ymm3, %ymm0, %ymm0
     ddc: c5 f5 58 c0                  	vaddpd	%ymm0, %ymm1, %ymm0
     de0: c4 a1 7d 10 0c f9            	vmovupd	(%rcx,%r15,8), %ymm1
     de6: c4 a1 7d 10 14 fa            	vmovupd	(%rdx,%r15,8), %ymm2
     dec: c5 f5 58 dc                  	vaddpd	%ymm4, %ymm1, %ymm3
     df0: c4 a1 7d 11 1c f9            	vmovupd	%ymm3, (%rcx,%r15,8)
     df6: c5 ed 58 d8                  	vaddpd	%ymm0, %ymm2, %ymm3
     dfa: c4 a1 7d 11 1c fa            	vmovupd	%ymm3, (%rdx,%r15,8)
     e00: c5 f5 5c cc                  	vsubpd	%ymm4, %ymm1, %ymm1
     e04: c4 a1 7d 11 0c fe            	vmovupd	%ymm1, (%rsi,%r15,8)
     e0a: c5 ed 5c c0                  	vsubpd	%ymm0, %ymm2, %ymm0
     e0e: c4 a1 7d 11 04 fb            	vmovupd	%ymm0, (%rbx,%r15,8)
     e14: 49 83 c7 04                  	addq	$0x4, %r15
     e18: 4d 39 d7                     	cmpq	%r10, %r15
     e1b: 72 93                        	jb	0xdb0 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x100>
     e1d: 4d 01 de                     	addq	%r11, %r14
     e20: 48 01 fe                     	addq	%rdi, %rsi
     e23: 48 01 fb                     	addq	%rdi, %rbx
     e26: 48 01 f9                     	addq	%rdi, %rcx
     e29: 48 01 fa                     	addq	%rdi, %rdx
     e2c: 49 39 c6                     	cmpq	%rax, %r14
     e2f: 0f 82 6b ff ff ff            	jb	0xda0 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xf0>
     e35: e9 a3 00 00 00               	jmp	0xedd <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x22d>
     e3a: 48 85 c0                     	testq	%rax, %rax
     e3d: 0f 84 9a 00 00 00            	je	0xedd <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x22d>
     e43: 4f 8d 1c 12                  	leaq	(%r10,%r10), %r11
     e47: 31 f6                        	xorl	%esi, %esi
     e49: 0f 1f 80 00 00 00 00         	nopl	(%rax)
     e50: 31 ff                        	xorl	%edi, %edi
     e52: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
     e60: 48 8d 1c 37                  	leaq	(%rdi,%rsi), %rbx
     e64: 4e 8d 34 13                  	leaq	(%rbx,%r10), %r14
     e68: c4 c1 79 10 04 f8            	vmovupd	(%r8,%rdi,8), %xmm0
     e6e: c4 c1 79 10 0c f9            	vmovupd	(%r9,%rdi,8), %xmm1
     e74: c4 a1 79 10 14 f1            	vmovupd	(%rcx,%r14,8), %xmm2
     e7a: c4 a1 79 10 1c f2            	vmovupd	(%rdx,%r14,8), %xmm3
     e80: c5 f9 59 e2                  	vmulpd	%xmm2, %xmm0, %xmm4
     e84: c5 f1 59 eb                  	vmulpd	%xmm3, %xmm1, %xmm5
     e88: c5 d9 5c e5                  	vsubpd	%xmm5, %xmm4, %xmm4
     e8c: c5 f1 59 ca                  	vmulpd	%xmm2, %xmm1, %xmm1
     e90: c5 f9 59 c3                  	vmulpd	%xmm3, %xmm0, %xmm0
     e94: c5 f1 58 c0                  	vaddpd	%xmm0, %xmm1, %xmm0
     e98: c5 f9 10 0c d9               	vmovupd	(%rcx,%rbx,8), %xmm1
     e9d: c5 f9 10 14 da               	vmovupd	(%rdx,%rbx,8), %xmm2
     ea2: c5 f1 58 dc                  	vaddpd	%xmm4, %xmm1, %xmm3
     ea6: c5 f9 11 1c d9               	vmovupd	%xmm3, (%rcx,%rbx,8)
     eab: c5 e9 58 d8                  	vaddpd	%xmm0, %xmm2, %xmm3
     eaf: c5 f9 11 1c da               	vmovupd	%xmm3, (%rdx,%rbx,8)
     eb4: c5 f1 5c cc                  	vsubpd	%xmm4, %xmm1, %xmm1
     eb8: c4 a1 79 11 0c f1            	vmovupd	%xmm1, (%rcx,%r14,8)
     ebe: c5 e9 5c c0                  	vsubpd	%xmm0, %xmm2, %xmm0
     ec2: c4 a1 79 11 04 f2            	vmovupd	%xmm0, (%rdx,%r14,8)
     ec8: 48 83 c7 02                  	addq	$0x2, %rdi
     ecc: 4c 39 d7                     	cmpq	%r10, %rdi
     ecf: 72 8f                        	jb	0xe60 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x1b0>
     ed1: 4c 01 de                     	addq	%r11, %rsi
     ed4: 48 39 c6                     	cmpq	%rax, %rsi
     ed7: 0f 82 73 ff ff ff            	jb	0xe50 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x1a0>
     edd: c5 f8 28 34 24               	vmovaps	(%rsp), %xmm6
     ee2: c5 f8 28 7c 24 10            	vmovaps	0x10(%rsp), %xmm7
     ee8: 48 83 c4 20                  	addq	$0x20, %rsp
     eec: 5b                           	popq	%rbx
     eed: 5f                           	popq	%rdi
     eee: 5e                           	popq	%rsi
     eef: 41 5e                        	popq	%r14
     ef1: 41 5f                        	popq	%r15
     ef3: c5 f8 77                     	vzeroupper
     ef6: c3                           	retq
     ef7: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)

0000000000000f00 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)>:
     f00: 48 83 ec 28                  	subq	$0x28, %rsp
     f04: 80 f9 02                     	cmpb	$0x2, %cl
     f07: 73 10                        	jae	0xf19 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x19>
     f09: 84 c9                        	testb	%cl, %cl
     f0b: 74 3a                        	je	0xf47 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x47>
     f0d: 48 8d 05 00 00 00 00         	leaq	(%rip), %rax            # 0xf14 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x14>
     f14: 48 83 c4 28                  	addq	$0x28, %rsp
     f18: c3                           	retq
     f19: 80 f9 03                     	cmpb	$0x3, %cl
     f1c: 74 1d                        	je	0xf3b <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x3b>
     f1e: 0f b6 c1                     	movzbl	%cl, %eax
     f21: 83 f8 04                     	cmpl	$0x4, %eax
     f24: 75 3f                        	jne	0xf65 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x65>
     f26: f6 05 0c 00 00 00 04         	testb	$0x4, 0xc(%rip)         # 0xf39 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x39>
     f2d: 74 36                        	je	0xf65 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x65>
     f2f: 48 8d 05 00 00 00 00         	leaq	(%rip), %rax            # 0xf36 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x36>
     f36: 48 83 c4 28                  	addq	$0x28, %rsp
     f3a: c3                           	retq
     f3b: 48 8d 05 00 00 00 00         	leaq	(%rip), %rax            # 0xf42 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x42>
     f42: 48 83 c4 28                  	addq	$0x28, %rsp
     f46: c3                           	retq
     f47: f6 05 0c 00 00 00 04         	testb	$0x4, 0xc(%rip)         # 0xf5a <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x5a>
     f4e: 48 8d 0d 00 00 00 00         	leaq	(%rip), %rcx            # 0xf55 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x55>
     f55: 48 8d 05 00 00 00 00         	leaq	(%rip), %rax            # 0xf5c <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x5c>
     f5c: 48 0f 44 c1                  	cmoveq	%rcx, %rax
     f60: 48 83 c4 28                  	addq	$0x28, %rsp
     f64: c3                           	retq
     f65: b9 10 00 00 00               	movl	$0x10, %ecx
     f6a: e8 00 00 00 00               	callq	0xf6f <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x6f>
     f6f: c6 00 0f                     	movb	$0xf, (%rax)
     f72: 48 c7 40 08 00 00 00 00      	movq	$0x0, 0x8(%rax)
     f7a: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0xf81 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x81>
     f81: 48 89 c1                     	movq	%rax, %rcx
     f84: 45 31 c0                     	xorl	%r8d, %r8d
     f87: e8 00 00 00 00               	callq	0xf8c <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x8c>
     f8c: cc                           	int3
     f8d: 0f 1f 00                     	nopl	(%rax)

0000000000000f90 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))>:
     f90: 41 57                        	pushq	%r15
     f92: 41 56                        	pushq	%r14
     f94: 41 55                        	pushq	%r13
     f96: 41 54                        	pushq	%r12
     f98: 56                           	pushq	%rsi
     f99: 57                           	pushq	%rdi
     f9a: 55                           	pushq	%rbp
     f9b: 53                           	pushq	%rbx
     f9c: 48 83 ec 38                  	subq	$0x38, %rsp
     fa0: 4c 89 cf                     	movq	%r9, %rdi
     fa3: 4c 89 44 24 30               	movq	%r8, 0x30(%rsp)
     fa8: 49 89 d4                     	movq	%rdx, %r12
     fab: 48 89 cb                     	movq	%rcx, %rbx
     fae: 48 8b b4 24 a0 00 00 00      	movq	0xa0(%rsp), %rsi
     fb6: 8b 69 04                     	movl	0x4(%rcx), %ebp
     fb9: 4d 8b 31                     	movq	(%r9), %r14
     fbc: 48 85 ed                     	testq	%rbp, %rbp
     fbf: 74 34                        	je	0xff5 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x65>
     fc1: 4c 8d 2c ed 00 00 00 00      	leaq	(,%rbp,8), %r13
     fc9: 4c 89 f1                     	movq	%r14, %rcx
     fcc: 31 d2                        	xorl	%edx, %edx
     fce: 4d 89 e8                     	movq	%r13, %r8
     fd1: e8 00 00 00 00               	callq	0xfd6 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x46>
     fd6: 4c 8b 3e                     	movq	(%rsi), %r15
     fd9: 4c 89 f9                     	movq	%r15, %rcx
     fdc: 31 d2                        	xorl	%edx, %edx
     fde: 4d 89 e8                     	movq	%r13, %r8
     fe1: e8 00 00 00 00               	callq	0xfe6 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x56>
     fe6: 8b 03                        	movl	(%rbx), %eax
     fe8: 41 89 c3                     	movl	%eax, %r11d
     feb: 41 d1 eb                     	shrl	%r11d
     fee: 75 16                        	jne	0x1006 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x76>
     ff0: e9 c7 00 00 00               	jmp	0x10bc <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x12c>
     ff5: 4c 8b 3e                     	movq	(%rsi), %r15
     ff8: 8b 03                        	movl	(%rbx), %eax
     ffa: 41 89 c3                     	movl	%eax, %r11d
     ffd: 41 d1 eb                     	shrl	%r11d
    1000: 0f 84 b6 00 00 00            	je	0x10bc <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x12c>
    1006: 4c 8b 4b 08                  	movq	0x8(%rbx), %r9
    100a: 4c 8b 43 68                  	movq	0x68(%rbx), %r8
    100e: 49 8b 14 24                  	movq	(%r12), %rdx
    1012: 48 8b 8b 88 00 00 00         	movq	0x88(%rbx), %rcx
    1019: 41 83 fb 01                  	cmpl	$0x1, %r11d
    101d: 75 05                        	jne	0x1024 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x94>
    101f: 45 31 d2                     	xorl	%r10d, %r10d
    1022: eb 70                        	jmp	0x1094 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x104>
    1024: 45 89 dc                     	movl	%r11d, %r12d
    1027: 41 83 e4 fe                  	andl	$-0x2, %r12d
    102b: 45 31 d2                     	xorl	%r10d, %r10d
    102e: 66 90                        	nop
    1030: 47 8b 2c 91                  	movl	(%r9,%r10,4), %r13d
    1034: f2 42 0f 10 04 d2            	movsd	(%rdx,%r10,8), %xmm0
    103a: f2 43 0f 59 04 d0            	mulsd	(%r8,%r10,8), %xmm0
    1040: f2 43 0f 11 04 ee            	movsd	%xmm0, (%r14,%r13,8)
    1046: f2 42 0f 10 04 d2            	movsd	(%rdx,%r10,8), %xmm0
    104c: f2 42 0f 59 04 d1            	mulsd	(%rcx,%r10,8), %xmm0
    1052: f2 43 0f 11 04 ef            	movsd	%xmm0, (%r15,%r13,8)
    1058: 47 8b 6c 91 04               	movl	0x4(%r9,%r10,4), %r13d
    105d: f2 42 0f 10 44 d2 08         	movsd	0x8(%rdx,%r10,8), %xmm0
    1064: f2 43 0f 59 44 d0 08         	mulsd	0x8(%r8,%r10,8), %xmm0
    106b: f2 43 0f 11 04 ee            	movsd	%xmm0, (%r14,%r13,8)
    1071: f2 42 0f 10 44 d2 08         	movsd	0x8(%rdx,%r10,8), %xmm0
    1078: f2 42 0f 59 44 d1 08         	mulsd	0x8(%rcx,%r10,8), %xmm0
    107f: f2 43 0f 11 04 ef            	movsd	%xmm0, (%r15,%r13,8)
    1085: 49 83 c2 02                  	addq	$0x2, %r10
    1089: 4d 39 d4                     	cmpq	%r10, %r12
    108c: 75 a2                        	jne	0x1030 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0xa0>
    108e: 41 f6 c3 01                  	testb	$0x1, %r11b
    1092: 74 28                        	je	0x10bc <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x12c>
    1094: 47 8b 0c 91                  	movl	(%r9,%r10,4), %r9d
    1098: f2 42 0f 10 04 d2            	movsd	(%rdx,%r10,8), %xmm0
    109e: f2 43 0f 59 04 d0            	mulsd	(%r8,%r10,8), %xmm0
    10a4: f2 43 0f 11 04 ce            	movsd	%xmm0, (%r14,%r9,8)
    10aa: f2 42 0f 10 04 d2            	movsd	(%rdx,%r10,8), %xmm0
    10b0: f2 42 0f 59 04 d1            	mulsd	(%rcx,%r10,8), %xmm0
    10b6: f2 43 0f 11 04 cf            	movsd	%xmm0, (%r15,%r9,8)
    10bc: 83 fd 02                     	cmpl	$0x2, %ebp
    10bf: 72 46                        	jb	0x1107 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x177>
    10c1: 4c 8b b4 24 a8 00 00 00      	movq	0xa8(%rsp), %r14
    10c9: 41 bf 02 00 00 00            	movl	$0x2, %r15d
    10cf: 90                           	nop
    10d0: 44 89 f8                     	movl	%r15d, %eax
    10d3: d1 e8                        	shrl	%eax
    10d5: 48 8b 4b 28                  	movq	0x28(%rbx), %rcx
    10d9: 48 8b 53 48                  	movq	0x48(%rbx), %rdx
    10dd: 4c 8d 04 c1                  	leaq	(%rcx,%rax,8), %r8
    10e1: 49 83 c0 f8                  	addq	$-0x8, %r8
    10e5: 4c 8d 4c c2 f8               	leaq	-0x8(%rdx,%rax,8), %r9
    10ea: 48 8b 0f                     	movq	(%rdi), %rcx
    10ed: 48 8b 16                     	movq	(%rsi), %rdx
    10f0: 48 89 6c 24 28               	movq	%rbp, 0x28(%rsp)
    10f5: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
    10fa: 41 ff d6                     	callq	*%r14
    10fd: 45 01 ff                     	addl	%r15d, %r15d
    1100: 41 39 ef                     	cmpl	%ebp, %r15d
    1103: 76 cb                        	jbe	0x10d0 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x140>
    1105: 8b 03                        	movl	(%rbx), %eax
    1107: c1 e8 02                     	shrl	$0x2, %eax
    110a: 89 e9                        	movl	%ebp, %ecx
    110c: 29 c1                        	subl	%eax, %ecx
    110e: 0f 84 e5 00 00 00            	je	0x11f9 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x269>
    1114: 48 8b 17                     	movq	(%rdi), %rdx
    1117: 4c 8b 83 a8 00 00 00         	movq	0xa8(%rbx), %r8
    111e: 4c 8b 8b c8 00 00 00         	movq	0xc8(%rbx), %r9
    1125: 4c 8b 16                     	movq	(%rsi), %r10
    1128: 4c 8b 5c 24 30               	movq	0x30(%rsp), %r11
    112d: 4d 8b 1b                     	movq	(%r11), %r11
    1130: 41 89 ce                     	movl	%ecx, %r14d
    1133: 83 f9 10                     	cmpl	$0x10, %ecx
    1136: 72 1f                        	jb	0x1157 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1c7>
    1138: 4d 8d 7e ff                  	leaq	-0x1(%r14), %r15
    113c: 41 89 c4                     	movl	%eax, %r12d
    113f: 45 01 fc                     	addl	%r15d, %r12d
    1142: 41 0f 92 c4                  	setb	%r12b
    1146: 49 c1 ef 20                  	shrq	$0x20, %r15
    114a: 41 0f 95 c7                  	setne	%r15b
    114e: 45 08 e7                     	orb	%r12b, %r15b
    1151: 0f 84 78 02 00 00            	je	0x13cf <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x43f>
    1157: 45 31 ff                     	xorl	%r15d, %r15d
    115a: 4d 89 fc                     	movq	%r15, %r12
    115d: 41 f6 c6 01                  	testb	$0x1, %r14b
    1161: 74 2d                        	je	0x1190 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x200>
    1163: 46 8d 24 38                  	leal	(%rax,%r15), %r12d
    1167: f2 42 0f 10 04 e2            	movsd	(%rdx,%r12,8), %xmm0
    116d: f2 43 0f 59 04 f8            	mulsd	(%r8,%r15,8), %xmm0
    1173: f2 43 0f 10 0c e2            	movsd	(%r10,%r12,8), %xmm1
    1179: f2 43 0f 59 0c f9            	mulsd	(%r9,%r15,8), %xmm1
    117f: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    1183: f2 43 0f 11 04 fb            	movsd	%xmm0, (%r11,%r15,8)
    1189: 4d 89 fc                     	movq	%r15, %r12
    118c: 49 83 cc 01                  	orq	$0x1, %r12
    1190: 4d 8d 6e ff                  	leaq	-0x1(%r14), %r13
    1194: 4d 39 ef                     	cmpq	%r13, %r15
    1197: 74 60                        	je	0x11f9 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x269>
    1199: 0f 1f 80 00 00 00 00         	nopl	(%rax)
    11a0: 46 8d 3c 20                  	leal	(%rax,%r12), %r15d
    11a4: f2 42 0f 10 04 fa            	movsd	(%rdx,%r15,8), %xmm0
    11aa: f2 43 0f 59 04 e0            	mulsd	(%r8,%r12,8), %xmm0
    11b0: f2 43 0f 10 0c fa            	movsd	(%r10,%r15,8), %xmm1
    11b6: f2 43 0f 59 0c e1            	mulsd	(%r9,%r12,8), %xmm1
    11bc: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    11c0: f2 43 0f 11 04 e3            	movsd	%xmm0, (%r11,%r12,8)
    11c6: 46 8d 7c 20 01               	leal	0x1(%rax,%r12), %r15d
    11cb: f2 42 0f 10 04 fa            	movsd	(%rdx,%r15,8), %xmm0
    11d1: f2 43 0f 59 44 e0 08         	mulsd	0x8(%r8,%r12,8), %xmm0
    11d8: f2 43 0f 10 0c fa            	movsd	(%r10,%r15,8), %xmm1
    11de: f2 43 0f 59 4c e1 08         	mulsd	0x8(%r9,%r12,8), %xmm1
    11e5: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    11e9: f2 43 0f 11 44 e3 08         	movsd	%xmm0, 0x8(%r11,%r12,8)
    11f0: 49 83 c4 02                  	addq	$0x2, %r12
    11f4: 4d 39 e6                     	cmpq	%r12, %r14
    11f7: 75 a7                        	jne	0x11a0 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x210>
    11f9: 39 e9                        	cmpl	%ebp, %ecx
    11fb: 0f 83 bd 01 00 00            	jae	0x13be <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x42e>
    1201: 48 8b 07                     	movq	(%rdi), %rax
    1204: 48 8b 93 a8 00 00 00         	movq	0xa8(%rbx), %rdx
    120b: 4c 8b 83 c8 00 00 00         	movq	0xc8(%rbx), %r8
    1212: 4c 8b 0e                     	movq	(%rsi), %r9
    1215: 4c 8b 54 24 30               	movq	0x30(%rsp), %r10
    121a: 4d 8b 12                     	movq	(%r10), %r10
    121d: 89 c9                        	movl	%ecx, %ecx
    121f: 48 89 ee                     	movq	%rbp, %rsi
    1222: 48 29 ce                     	subq	%rcx, %rsi
    1225: 49 89 cb                     	movq	%rcx, %r11
    1228: 48 83 fe 0a                  	cmpq	$0xa, %rsi
    122c: 0f 82 e9 00 00 00            	jb	0x131b <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x38b>
    1232: 4d 8d 1c ca                  	leaq	(%r10,%rcx,8), %r11
    1236: 4c 89 df                     	movq	%r11, %rdi
    1239: 48 29 c7                     	subq	%rax, %rdi
    123c: 48 83 ff 20                  	cmpq	$0x20, %rdi
    1240: 0f 92 c3                     	setb	%bl
    1243: 4c 89 d7                     	movq	%r10, %rdi
    1246: 48 29 d7                     	subq	%rdx, %rdi
    1249: 48 83 ff 20                  	cmpq	$0x20, %rdi
    124d: 40 0f 92 c7                  	setb	%dil
    1251: 40 08 df                     	orb	%bl, %dil
    1254: 4d 29 cb                     	subq	%r9, %r11
    1257: 49 83 fb 20                  	cmpq	$0x20, %r11
    125b: 41 0f 92 c3                  	setb	%r11b
    125f: 4c 89 d3                     	movq	%r10, %rbx
    1262: 4c 29 c3                     	subq	%r8, %rbx
    1265: 48 83 fb 20                  	cmpq	$0x20, %rbx
    1269: 0f 92 c3                     	setb	%bl
    126c: 44 08 db                     	orb	%r11b, %bl
    126f: 40 08 fb                     	orb	%dil, %bl
    1272: 49 89 cb                     	movq	%rcx, %r11
    1275: 0f 85 a0 00 00 00            	jne	0x131b <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x38b>
    127b: 4c 8d 3c cd 00 00 00 00      	leaq	(,%rcx,8), %r15
    1283: 48 89 f7                     	movq	%rsi, %rdi
    1286: 48 83 e7 fc                  	andq	$-0x4, %rdi
    128a: 4c 8d 1c 0f                  	leaq	(%rdi,%rcx), %r11
    128e: 4b 8d 1c 3a                  	leaq	(%r10,%r15), %rbx
    1292: 48 83 c3 10                  	addq	$0x10, %rbx
    1296: 4f 8d 34 38                  	leaq	(%r8,%r15), %r14
    129a: 49 83 c6 10                  	addq	$0x10, %r14
    129e: 49 01 d7                     	addq	%rdx, %r15
    12a1: 49 83 c7 10                  	addq	$0x10, %r15
    12a5: 45 31 e4                     	xorl	%r12d, %r12d
    12a8: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
    12b0: 66 42 0f 10 04 e0            	movupd	(%rax,%r12,8), %xmm0
    12b6: 66 42 0f 10 4c e0 10         	movupd	0x10(%rax,%r12,8), %xmm1
    12bd: 66 43 0f 10 54 e7 f0         	movupd	-0x10(%r15,%r12,8), %xmm2
    12c4: 66 0f 59 d0                  	mulpd	%xmm0, %xmm2
    12c8: 66 43 0f 10 04 e7            	movupd	(%r15,%r12,8), %xmm0
    12ce: 66 0f 59 c1                  	mulpd	%xmm1, %xmm0
    12d2: 66 43 0f 10 0c e1            	movupd	(%r9,%r12,8), %xmm1
    12d8: 66 43 0f 10 5c e1 10         	movupd	0x10(%r9,%r12,8), %xmm3
    12df: 66 43 0f 10 64 e6 f0         	movupd	-0x10(%r14,%r12,8), %xmm4
    12e6: 66 0f 59 e1                  	mulpd	%xmm1, %xmm4
    12ea: 66 0f 5c d4                  	subpd	%xmm4, %xmm2
    12ee: 66 43 0f 10 0c e6            	movupd	(%r14,%r12,8), %xmm1
    12f4: 66 0f 59 cb                  	mulpd	%xmm3, %xmm1
    12f8: 66 0f 5c c1                  	subpd	%xmm1, %xmm0
    12fc: 66 42 0f 11 54 e3 f0         	movupd	%xmm2, -0x10(%rbx,%r12,8)
    1303: 66 42 0f 11 04 e3            	movupd	%xmm0, (%rbx,%r12,8)
    1309: 49 83 c4 04                  	addq	$0x4, %r12
    130d: 4c 39 e7                     	cmpq	%r12, %rdi
    1310: 75 9e                        	jne	0x12b0 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x320>
    1312: 48 39 fe                     	cmpq	%rdi, %rsi
    1315: 0f 84 a3 00 00 00            	je	0x13be <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x42e>
    131b: 89 ef                        	movl	%ebp, %edi
    131d: 44 29 df                     	subl	%r11d, %edi
    1320: 4c 89 de                     	movq	%r11, %rsi
    1323: 40 f6 c7 01                  	testb	$0x1, %dil
    1327: 74 2b                        	je	0x1354 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x3c4>
    1329: 4c 89 de                     	movq	%r11, %rsi
    132c: 48 29 ce                     	subq	%rcx, %rsi
    132f: f2 0f 10 04 f0               	movsd	(%rax,%rsi,8), %xmm0
    1334: f2 42 0f 59 04 da            	mulsd	(%rdx,%r11,8), %xmm0
    133a: f2 41 0f 10 0c f1            	movsd	(%r9,%rsi,8), %xmm1
    1340: f2 43 0f 59 0c d8            	mulsd	(%r8,%r11,8), %xmm1
    1346: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    134a: f2 43 0f 11 04 da            	movsd	%xmm0, (%r10,%r11,8)
    1350: 49 8d 73 01                  	leaq	0x1(%r11), %rsi
    1354: 48 8d 7d ff                  	leaq	-0x1(%rbp), %rdi
    1358: 49 39 fb                     	cmpq	%rdi, %r11
    135b: 74 61                        	je	0x13be <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x42e>
    135d: 48 c1 e1 03                  	shlq	$0x3, %rcx
    1361: 41 bb 08 00 00 00            	movl	$0x8, %r11d
    1367: 49 29 cb                     	subq	%rcx, %r11
    136a: 4c 01 d8                     	addq	%r11, %rax
    136d: 4d 01 d9                     	addq	%r11, %r9
    1370: f2 0f 10 44 f0 f8            	movsd	-0x8(%rax,%rsi,8), %xmm0
    1376: f2 0f 59 04 f2               	mulsd	(%rdx,%rsi,8), %xmm0
    137b: f2 41 0f 10 4c f1 f8         	movsd	-0x8(%r9,%rsi,8), %xmm1
    1382: f2 41 0f 59 0c f0            	mulsd	(%r8,%rsi,8), %xmm1
    1388: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    138c: f2 41 0f 11 04 f2            	movsd	%xmm0, (%r10,%rsi,8)
    1392: f2 0f 10 04 f0               	movsd	(%rax,%rsi,8), %xmm0
    1397: f2 0f 59 44 f2 08            	mulsd	0x8(%rdx,%rsi,8), %xmm0
    139d: f2 41 0f 10 0c f1            	movsd	(%r9,%rsi,8), %xmm1
    13a3: f2 41 0f 59 4c f0 08         	mulsd	0x8(%r8,%rsi,8), %xmm1
    13aa: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    13ae: f2 41 0f 11 44 f2 08         	movsd	%xmm0, 0x8(%r10,%rsi,8)
    13b5: 48 83 c6 02                  	addq	$0x2, %rsi
    13b9: 48 39 f5                     	cmpq	%rsi, %rbp
    13bc: 75 b2                        	jne	0x1370 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x3e0>
    13be: 48 83 c4 38                  	addq	$0x38, %rsp
    13c2: 5b                           	popq	%rbx
    13c3: 5d                           	popq	%rbp
    13c4: 5f                           	popq	%rdi
    13c5: 5e                           	popq	%rsi
    13c6: 41 5c                        	popq	%r12
    13c8: 41 5d                        	popq	%r13
    13ca: 41 5e                        	popq	%r14
    13cc: 41 5f                        	popq	%r15
    13ce: c3                           	retq
    13cf: 4d 89 dc                     	movq	%r11, %r12
    13d2: 4d 29 c4                     	subq	%r8, %r12
    13d5: 45 31 ff                     	xorl	%r15d, %r15d
    13d8: 49 83 fc 20                  	cmpq	$0x20, %r12
    13dc: 0f 82 78 fd ff ff            	jb	0x115a <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    13e2: 4d 89 dc                     	movq	%r11, %r12
    13e5: 4d 29 cc                     	subq	%r9, %r12
    13e8: 49 83 fc 20                  	cmpq	$0x20, %r12
    13ec: 0f 82 68 fd ff ff            	jb	0x115a <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    13f2: 4c 8d 24 c2                  	leaq	(%rdx,%rax,8), %r12
    13f6: 4d 89 dd                     	movq	%r11, %r13
    13f9: 4d 29 e5                     	subq	%r12, %r13
    13fc: 49 83 fd 20                  	cmpq	$0x20, %r13
    1400: 0f 82 54 fd ff ff            	jb	0x115a <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    1406: 4d 8d 24 c2                  	leaq	(%r10,%rax,8), %r12
    140a: 4d 89 dd                     	movq	%r11, %r13
    140d: 4d 29 e5                     	subq	%r12, %r13
    1410: 49 83 fd 20                  	cmpq	$0x20, %r13
    1414: 0f 82 40 fd ff ff            	jb	0x115a <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    141a: 45 89 f7                     	movl	%r14d, %r15d
    141d: 41 83 e7 fc                  	andl	$-0x4, %r15d
    1421: 45 31 e4                     	xorl	%r12d, %r12d
    1424: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
    1430: 46 8d 2c 20                  	leal	(%rax,%r12), %r13d
    1434: 66 42 0f 10 04 ea            	movupd	(%rdx,%r13,8), %xmm0
    143a: 66 42 0f 10 4c ea 10         	movupd	0x10(%rdx,%r13,8), %xmm1
    1441: 66 43 0f 10 14 e0            	movupd	(%r8,%r12,8), %xmm2
    1447: 66 0f 59 d0                  	mulpd	%xmm0, %xmm2
    144b: 66 43 0f 10 44 e0 10         	movupd	0x10(%r8,%r12,8), %xmm0
    1452: 66 0f 59 c1                  	mulpd	%xmm1, %xmm0
    1456: 66 43 0f 10 0c ea            	movupd	(%r10,%r13,8), %xmm1
    145c: 66 43 0f 10 5c ea 10         	movupd	0x10(%r10,%r13,8), %xmm3
    1463: 66 43 0f 10 24 e1            	movupd	(%r9,%r12,8), %xmm4
    1469: 66 0f 59 e1                  	mulpd	%xmm1, %xmm4
    146d: 66 0f 5c d4                  	subpd	%xmm4, %xmm2
    1471: 66 43 0f 10 4c e1 10         	movupd	0x10(%r9,%r12,8), %xmm1
    1478: 66 0f 59 cb                  	mulpd	%xmm3, %xmm1
    147c: 66 0f 5c c1                  	subpd	%xmm1, %xmm0
    1480: 66 43 0f 11 14 e3            	movupd	%xmm2, (%r11,%r12,8)
    1486: 66 43 0f 11 44 e3 10         	movupd	%xmm0, 0x10(%r11,%r12,8)
    148d: 49 83 c4 04                  	addq	$0x4, %r12
    1491: 4d 39 e7                     	cmpq	%r12, %r15
    1494: 75 9a                        	jne	0x1430 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x4a0>
    1496: 45 39 f7                     	cmpl	%r14d, %r15d
    1499: 0f 85 bb fc ff ff            	jne	0x115a <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    149f: e9 55 fd ff ff               	jmp	0x11f9 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x269>
    14a4: 48 89 c1                     	movq	%rax, %rcx
    14a7: e8 00 00 00 00               	callq	0x14ac <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x51c>
    14ac: cc                           	int3

Disassembly of section .text$_ZNSt3__16vectorIjNS_3pmr21polymorphic_allocatorIjEEE6resizeEy:

0000000000000000 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)>:
       0: 41 57                        	pushq	%r15
       2: 41 56                        	pushq	%r14
       4: 41 55                        	pushq	%r13
       6: 41 54                        	pushq	%r12
       8: 56                           	pushq	%rsi
       9: 57                           	pushq	%rdi
       a: 55                           	pushq	%rbp
       b: 53                           	pushq	%rbx
       c: 48 83 ec 28                  	subq	$0x28, %rsp
      10: 48 89 ce                     	movq	%rcx, %rsi
      13: 48 8b 39                     	movq	(%rcx), %rdi
      16: 4c 8b 71 08                  	movq	0x8(%rcx), %r14
      1a: 4d 89 f5                     	movq	%r14, %r13
      1d: 49 29 fd                     	subq	%rdi, %r13
      20: 4c 89 e8                     	movq	%r13, %rax
      23: 48 c1 f8 02                  	sarq	$0x2, %rax
      27: 49 89 d7                     	movq	%rdx, %r15
      2a: 49 29 c7                     	subq	%rax, %r15
      2d: 0f 86 7a 01 00 00            	jbe	0x1ad <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1ad>
      33: 48 8b 46 10                  	movq	0x10(%rsi), %rax
      37: 48 29 f8                     	subq	%rdi, %rax
      3a: 48 89 c1                     	movq	%rax, %rcx
      3d: 48 c1 f9 02                  	sarq	$0x2, %rcx
      41: 48 39 ca                     	cmpq	%rcx, %rdx
      44: 0f 86 6b 01 00 00            	jbe	0x1b5 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1b5>
      4a: 48 89 d1                     	movq	%rdx, %rcx
      4d: 48 c1 e9 3e                  	shrq	$0x3e, %rcx
      51: 0f 85 88 01 00 00            	jne	0x1df <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1df>
      57: 48 b9 ff ff ff ff ff ff ff 3f	movabsq	$0x3fffffffffffffff, %rcx # imm = 0x3FFFFFFFFFFFFFFF
      61: 49 b8 f8 ff ff ff ff ff ff 7f	movabsq	$0x7ffffffffffffff8, %r8 # imm = 0x7FFFFFFFFFFFFFF8
      6b: 49 83 c0 04                  	addq	$0x4, %r8
      6f: 49 89 c4                     	movq	%rax, %r12
      72: 49 d1 fc                     	sarq	%r12
      75: 49 39 d4                     	cmpq	%rdx, %r12
      78: 4c 0f 46 e2                  	cmovbeq	%rdx, %r12
      7c: 4c 39 c0                     	cmpq	%r8, %rax
      7f: 4c 0f 43 e1                  	cmovaeq	%rcx, %r12
      83: 49 39 cc                     	cmpq	%rcx, %r12
      86: 0f 87 58 01 00 00            	ja	0x1e4 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1e4>
      8c: 48 8b 4e 18                  	movq	0x18(%rsi), %rcx
      90: 4a 8d 14 a5 00 00 00 00      	leaq	(,%r12,4), %rdx
      98: 48 8b 01                     	movq	(%rcx), %rax
      9b: 41 b8 04 00 00 00            	movl	$0x4, %r8d
      a1: 48 89 4c 24 20               	movq	%rcx, 0x20(%rsp)
      a6: ff 50 10                     	callq	*0x10(%rax)
      a9: 48 89 c3                     	movq	%rax, %rbx
      ac: 4a 8d 2c 28                  	leaq	(%rax,%r13), %rbp
      b0: 4e 8d 04 bd 00 00 00 00      	leaq	(,%r15,4), %r8
      b8: 48 89 e9                     	movq	%rbp, %rcx
      bb: 31 d2                        	xorl	%edx, %edx
      bd: e8 00 00 00 00               	callq	0xc2 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0xc2>
      c2: 48 8b 16                     	movq	(%rsi), %rdx
      c5: 48 8b 4e 08                  	movq	0x8(%rsi), %rcx
      c9: 48 89 c8                     	movq	%rcx, %rax
      cc: 48 29 d0                     	subq	%rdx, %rax
      cf: 4e 8d 3c bd 00 00 00 00      	leaq	(,%r15,4), %r15
      d7: 49 01 ef                     	addq	%rbp, %r15
      da: 48 29 c5                     	subq	%rax, %rbp
      dd: 49 89 c8                     	movq	%rcx, %r8
      e0: 4e 8d 0c a3                  	leaq	(%rbx,%r12,4), %r9
      e4: 49 29 d0                     	subq	%rdx, %r8
      e7: 0f 84 96 00 00 00            	je	0x183 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x183>
      ed: 49 83 c0 fc                  	addq	$-0x4, %r8
      f1: 49 89 d2                     	movq	%rdx, %r10
      f4: 49 89 eb                     	movq	%rbp, %r11
      f7: 49 83 f8 4c                  	cmpq	$0x4c, %r8
      fb: 72 73                        	jb	0x170 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x170>
      fd: 49 01 de                     	addq	%rbx, %r14
     100: 48 01 cf                     	addq	%rcx, %rdi
     103: 49 29 fe                     	subq	%rdi, %r14
     106: 49 89 d2                     	movq	%rdx, %r10
     109: 49 89 eb                     	movq	%rbp, %r11
     10c: 49 83 fe 20                  	cmpq	$0x20, %r14
     110: 72 5e                        	jb	0x170 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x170>
     112: 48 c1 f8 02                  	sarq	$0x2, %rax
     116: 49 c1 e8 02                  	shrq	$0x2, %r8
     11a: 49 ff c0                     	incq	%r8
     11d: 49 be f8 ff ff ff ff ff ff 7f	movabsq	$0x7ffffffffffffff8, %r14 # imm = 0x7FFFFFFFFFFFFFF8
     127: 4d 21 c6                     	andq	%r8, %r14
     12a: 4e 8d 14 b2                  	leaq	(%rdx,%r14,4), %r10
     12e: 4e 8d 1c b5 00 00 00 00      	leaq	(,%r14,4), %r11
     136: 49 01 eb                     	addq	%rbp, %r11
     139: 48 c1 e0 02                  	shlq	$0x2, %rax
     13d: 49 29 c5                     	subq	%rax, %r13
     140: 4a 8d 04 2b                  	leaq	(%rbx,%r13), %rax
     144: 48 83 c0 10                  	addq	$0x10, %rax
     148: 31 ff                        	xorl	%edi, %edi
     14a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     150: 0f 10 04 ba                  	movups	(%rdx,%rdi,4), %xmm0
     154: 0f 10 4c ba 10               	movups	0x10(%rdx,%rdi,4), %xmm1
     159: 0f 11 44 b8 f0               	movups	%xmm0, -0x10(%rax,%rdi,4)
     15e: 0f 11 0c b8                  	movups	%xmm1, (%rax,%rdi,4)
     162: 48 83 c7 08                  	addq	$0x8, %rdi
     166: 49 39 fe                     	cmpq	%rdi, %r14
     169: 75 e5                        	jne	0x150 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x150>
     16b: 4d 39 f0                     	cmpq	%r14, %r8
     16e: 74 13                        	je	0x183 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x183>
     170: 41 8b 02                     	movl	(%r10), %eax
     173: 41 89 03                     	movl	%eax, (%r11)
     176: 49 83 c2 04                  	addq	$0x4, %r10
     17a: 49 83 c3 04                  	addq	$0x4, %r11
     17e: 49 39 ca                     	cmpq	%rcx, %r10
     181: 75 ed                        	jne	0x170 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x170>
     183: 4c 8b 46 10                  	movq	0x10(%rsi), %r8
     187: 48 89 2e                     	movq	%rbp, (%rsi)
     18a: 4c 89 7e 08                  	movq	%r15, 0x8(%rsi)
     18e: 4c 89 4e 10                  	movq	%r9, 0x10(%rsi)
     192: 48 85 d2                     	testq	%rdx, %rdx
     195: 74 37                        	je	0x1ce <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1ce>
     197: 49 29 d0                     	subq	%rdx, %r8
     19a: 48 8b 4c 24 20               	movq	0x20(%rsp), %rcx
     19f: 48 8b 01                     	movq	(%rcx), %rax
     1a2: 41 b9 04 00 00 00            	movl	$0x4, %r9d
     1a8: ff 50 18                     	callq	*0x18(%rax)
     1ab: eb 21                        	jmp	0x1ce <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1ce>
     1ad: 73 1f                        	jae	0x1ce <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1ce>
     1af: 48 8d 3c 97                  	leaq	(%rdi,%rdx,4), %rdi
     1b3: eb 15                        	jmp	0x1ca <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1ca>
     1b5: 4b 8d 3c be                  	leaq	(%r14,%r15,4), %rdi
     1b9: 49 c1 e7 02                  	shlq	$0x2, %r15
     1bd: 4c 89 f1                     	movq	%r14, %rcx
     1c0: 31 d2                        	xorl	%edx, %edx
     1c2: 4d 89 f8                     	movq	%r15, %r8
     1c5: e8 00 00 00 00               	callq	0x1ca <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1ca>
     1ca: 48 89 7e 08                  	movq	%rdi, 0x8(%rsi)
     1ce: 48 83 c4 28                  	addq	$0x28, %rsp
     1d2: 5b                           	popq	%rbx
     1d3: 5d                           	popq	%rbp
     1d4: 5f                           	popq	%rdi
     1d5: 5e                           	popq	%rsi
     1d6: 41 5c                        	popq	%r12
     1d8: 41 5d                        	popq	%r13
     1da: 41 5e                        	popq	%r14
     1dc: 41 5f                        	popq	%r15
     1de: c3                           	retq
     1df: e8 00 00 00 00               	callq	0x1e4 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1e4>
     1e4: e8 00 00 00 00               	callq	0x1e9 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1e9>
     1e9: 48 89 c1                     	movq	%rax, %rcx
     1ec: e8 00 00 00 00               	callq	0x1f1 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::resize(unsigned long long)+0x1f1>
     1f1: cc                           	int3

Disassembly of section .text$_ZNSt3__16vectorIdNS_3pmr21polymorphic_allocatorIdEEE6resizeEy:

0000000000000000 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)>:
       0: 41 57                        	pushq	%r15
       2: 41 56                        	pushq	%r14
       4: 41 55                        	pushq	%r13
       6: 41 54                        	pushq	%r12
       8: 56                           	pushq	%rsi
       9: 57                           	pushq	%rdi
       a: 55                           	pushq	%rbp
       b: 53                           	pushq	%rbx
       c: 48 83 ec 28                  	subq	$0x28, %rsp
      10: 48 89 ce                     	movq	%rcx, %rsi
      13: 48 8b 29                     	movq	(%rcx), %rbp
      16: 4c 8b 71 08                  	movq	0x8(%rcx), %r14
      1a: 4d 89 f5                     	movq	%r14, %r13
      1d: 49 29 ed                     	subq	%rbp, %r13
      20: 4c 89 e8                     	movq	%r13, %rax
      23: 48 c1 f8 03                  	sarq	$0x3, %rax
      27: 49 89 d7                     	movq	%rdx, %r15
      2a: 49 29 c7                     	subq	%rax, %r15
      2d: 0f 86 7e 01 00 00            	jbe	0x1b1 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1b1>
      33: 48 8b 46 10                  	movq	0x10(%rsi), %rax
      37: 48 29 e8                     	subq	%rbp, %rax
      3a: 48 89 c1                     	movq	%rax, %rcx
      3d: 48 c1 f9 03                  	sarq	$0x3, %rcx
      41: 48 39 ca                     	cmpq	%rcx, %rdx
      44: 0f 86 76 01 00 00            	jbe	0x1c0 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1c0>
      4a: 48 89 d1                     	movq	%rdx, %rcx
      4d: 48 c1 e9 3d                  	shrq	$0x3d, %rcx
      51: 0f 85 93 01 00 00            	jne	0x1ea <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1ea>
      57: 48 b9 ff ff ff ff ff ff ff 1f	movabsq	$0x1fffffffffffffff, %rcx # imm = 0x1FFFFFFFFFFFFFFF
      61: 48 89 c7                     	movq	%rax, %rdi
      64: 48 c1 ff 02                  	sarq	$0x2, %rdi
      68: 48 39 d7                     	cmpq	%rdx, %rdi
      6b: 48 0f 46 fa                  	cmovbeq	%rdx, %rdi
      6f: 48 ba f8 ff ff ff ff ff ff 7f	movabsq	$0x7ffffffffffffff8, %rdx # imm = 0x7FFFFFFFFFFFFFF8
      79: 48 39 d0                     	cmpq	%rdx, %rax
      7c: 48 0f 43 f9                  	cmovaeq	%rcx, %rdi
      80: 48 39 cf                     	cmpq	%rcx, %rdi
      83: 0f 87 66 01 00 00            	ja	0x1ef <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1ef>
      89: 48 8b 4e 18                  	movq	0x18(%rsi), %rcx
      8d: 48 8d 14 fd 00 00 00 00      	leaq	(,%rdi,8), %rdx
      95: 48 8b 01                     	movq	(%rcx), %rax
      98: 41 b8 08 00 00 00            	movl	$0x8, %r8d
      9e: 48 89 4c 24 20               	movq	%rcx, 0x20(%rsp)
      a3: ff 50 10                     	callq	*0x10(%rax)
      a6: 49 89 c4                     	movq	%rax, %r12
      a9: 4a 8d 1c 28                  	leaq	(%rax,%r13), %rbx
      ad: 4e 8d 04 fd 00 00 00 00      	leaq	(,%r15,8), %r8
      b5: 48 89 d9                     	movq	%rbx, %rcx
      b8: 31 d2                        	xorl	%edx, %edx
      ba: e8 00 00 00 00               	callq	0xbf <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0xbf>
      bf: 48 8b 16                     	movq	(%rsi), %rdx
      c2: 48 8b 4e 08                  	movq	0x8(%rsi), %rcx
      c6: 48 89 c8                     	movq	%rcx, %rax
      c9: 48 29 d0                     	subq	%rdx, %rax
      cc: 4e 8d 3c fb                  	leaq	(%rbx,%r15,8), %r15
      d0: 48 29 c3                     	subq	%rax, %rbx
      d3: 49 89 c8                     	movq	%rcx, %r8
      d6: 4d 8d 0c fc                  	leaq	(%r12,%rdi,8), %r9
      da: 49 29 d0                     	subq	%rdx, %r8
      dd: 0f 84 a4 00 00 00            	je	0x187 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x187>
      e3: 49 83 c0 f8                  	addq	$-0x8, %r8
      e7: 49 89 d2                     	movq	%rdx, %r10
      ea: 49 89 db                     	movq	%rbx, %r11
      ed: 49 81 f8 98 00 00 00         	cmpq	$0x98, %r8
      f4: 72 7a                        	jb	0x170 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x170>
      f6: 4d 01 e6                     	addq	%r12, %r14
      f9: 48 01 cd                     	addq	%rcx, %rbp
      fc: 49 29 ee                     	subq	%rbp, %r14
      ff: 49 89 d2                     	movq	%rdx, %r10
     102: 49 89 db                     	movq	%rbx, %r11
     105: 49 83 fe 20                  	cmpq	$0x20, %r14
     109: 72 65                        	jb	0x170 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x170>
     10b: 48 c1 f8 03                  	sarq	$0x3, %rax
     10f: 49 c1 e8 03                  	shrq	$0x3, %r8
     113: 49 ff c0                     	incq	%r8
     116: 4c 89 c7                     	movq	%r8, %rdi
     119: 48 83 e7 fc                  	andq	$-0x4, %rdi
     11d: 4c 8d 14 fa                  	leaq	(%rdx,%rdi,8), %r10
     121: 4c 8d 1c fb                  	leaq	(%rbx,%rdi,8), %r11
     125: 48 c1 e0 03                  	shlq	$0x3, %rax
     129: 49 29 c5                     	subq	%rax, %r13
     12c: 4b 8d 04 2c                  	leaq	(%r12,%r13), %rax
     130: 48 83 c0 10                  	addq	$0x10, %rax
     134: 45 31 f6                     	xorl	%r14d, %r14d
     137: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
     140: 42 0f 10 04 f2               	movups	(%rdx,%r14,8), %xmm0
     145: 42 0f 10 4c f2 10            	movups	0x10(%rdx,%r14,8), %xmm1
     14b: 42 0f 11 44 f0 f0            	movups	%xmm0, -0x10(%rax,%r14,8)
     151: 42 0f 11 0c f0               	movups	%xmm1, (%rax,%r14,8)
     156: 49 83 c6 04                  	addq	$0x4, %r14
     15a: 4c 39 f7                     	cmpq	%r14, %rdi
     15d: 75 e1                        	jne	0x140 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x140>
     15f: 49 39 f8                     	cmpq	%rdi, %r8
     162: 74 23                        	je	0x187 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x187>
     164: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
     170: f2 41 0f 10 02               	movsd	(%r10), %xmm0
     175: f2 41 0f 11 03               	movsd	%xmm0, (%r11)
     17a: 49 83 c2 08                  	addq	$0x8, %r10
     17e: 49 83 c3 08                  	addq	$0x8, %r11
     182: 49 39 ca                     	cmpq	%rcx, %r10
     185: 75 e9                        	jne	0x170 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x170>
     187: 4c 8b 46 10                  	movq	0x10(%rsi), %r8
     18b: 48 89 1e                     	movq	%rbx, (%rsi)
     18e: 4c 89 7e 08                  	movq	%r15, 0x8(%rsi)
     192: 4c 89 4e 10                  	movq	%r9, 0x10(%rsi)
     196: 48 85 d2                     	testq	%rdx, %rdx
     199: 74 3e                        	je	0x1d9 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1d9>
     19b: 49 29 d0                     	subq	%rdx, %r8
     19e: 48 8b 4c 24 20               	movq	0x20(%rsp), %rcx
     1a3: 48 8b 01                     	movq	(%rcx), %rax
     1a6: 41 b9 08 00 00 00            	movl	$0x8, %r9d
     1ac: ff 50 18                     	callq	*0x18(%rax)
     1af: eb 28                        	jmp	0x1d9 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1d9>
     1b1: 73 26                        	jae	0x1d9 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1d9>
     1b3: 48 8d 3c d5 00 00 00 00      	leaq	(,%rdx,8), %rdi
     1bb: 48 01 ef                     	addq	%rbp, %rdi
     1be: eb 15                        	jmp	0x1d5 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1d5>
     1c0: 4b 8d 3c fe                  	leaq	(%r14,%r15,8), %rdi
     1c4: 49 c1 e7 03                  	shlq	$0x3, %r15
     1c8: 4c 89 f1                     	movq	%r14, %rcx
     1cb: 31 d2                        	xorl	%edx, %edx
     1cd: 4d 89 f8                     	movq	%r15, %r8
     1d0: e8 00 00 00 00               	callq	0x1d5 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1d5>
     1d5: 48 89 7e 08                  	movq	%rdi, 0x8(%rsi)
     1d9: 48 83 c4 28                  	addq	$0x28, %rsp
     1dd: 5b                           	popq	%rbx
     1de: 5d                           	popq	%rbp
     1df: 5f                           	popq	%rdi
     1e0: 5e                           	popq	%rsi
     1e1: 41 5c                        	popq	%r12
     1e3: 41 5d                        	popq	%r13
     1e5: 41 5e                        	popq	%r14
     1e7: 41 5f                        	popq	%r15
     1e9: c3                           	retq
     1ea: e8 00 00 00 00               	callq	0x1ef <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1ef>
     1ef: e8 00 00 00 00               	callq	0x1f4 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1f4>
     1f4: 48 89 c1                     	movq	%rax, %rcx
     1f7: e8 00 00 00 00               	callq	0x1fc <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::resize(unsigned long long)+0x1fc>
     1fc: cc                           	int3

Disassembly of section .text$__clang_call_terminate:

0000000000000000 <__clang_call_terminate>:
       0: 48 83 ec 28                  	subq	$0x28, %rsp
       4: e8 00 00 00 00               	callq	0x9 <__clang_call_terminate+0x9>
       9: e8 00 00 00 00               	callq	0xe <__clang_call_terminate+0xe>
       e: cc                           	int3

Disassembly of section .text$_ZNSt3__16vectorIjNS_3pmr21polymorphic_allocatorIjEEE20__throw_length_errorB9nqe220108Ev:

0000000000000000 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::__throw_length_error[abi:nqe220108]()>:
       0: 48 83 ec 28                  	subq	$0x28, %rsp
       4: 48 8d 0d 48 00 00 00         	leaq	0x48(%rip), %rcx        # 0x53 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::__throw_length_error[abi:nqe220108]()+0x53>
       b: e8 00 00 00 00               	callq	0x10 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::__throw_length_error[abi:nqe220108]()+0x10>
      10: cc                           	int3

Disassembly of section .text$_ZNSt3__120__throw_length_errorB9nqe220108EPKc:

0000000000000000 <std::__1::__throw_length_error[abi:nqe220108](char const*)>:
       0: 56                           	pushq	%rsi
       1: 57                           	pushq	%rdi
       2: 48 83 ec 28                  	subq	$0x28, %rsp
       6: 48 89 cf                     	movq	%rcx, %rdi
       9: b9 10 00 00 00               	movl	$0x10, %ecx
       e: e8 00 00 00 00               	callq	0x13 <std::__1::__throw_length_error[abi:nqe220108](char const*)+0x13>
      13: 48 89 c6                     	movq	%rax, %rsi
      16: 48 89 c1                     	movq	%rax, %rcx
      19: 48 89 fa                     	movq	%rdi, %rdx
      1c: e8 00 00 00 00               	callq	0x21 <std::__1::__throw_length_error[abi:nqe220108](char const*)+0x21>
      21: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x28 <std::__1::__throw_length_error[abi:nqe220108](char const*)+0x28>
      28: 4c 8d 05 00 00 00 00         	leaq	(%rip), %r8             # 0x2f <std::__1::__throw_length_error[abi:nqe220108](char const*)+0x2f>
      2f: 48 89 f1                     	movq	%rsi, %rcx
      32: e8 00 00 00 00               	callq	0x37 <std::__1::__throw_length_error[abi:nqe220108](char const*)+0x37>
      37: 48 89 c7                     	movq	%rax, %rdi
      3a: 48 89 f1                     	movq	%rsi, %rcx
      3d: e8 00 00 00 00               	callq	0x42 <std::__1::__throw_length_error[abi:nqe220108](char const*)+0x42>
      42: 48 89 f9                     	movq	%rdi, %rcx
      45: e8 00 00 00 00               	callq	0x4a <std::__1::__throw_length_error[abi:nqe220108](char const*)+0x4a>
      4a: cc                           	int3

Disassembly of section .text$_ZNSt12length_errorC2B9nqe220108EPKc:

0000000000000000 <std::length_error::length_error[abi:nqe220108](char const*)>:
       0: 56                           	pushq	%rsi
       1: 48 83 ec 20                  	subq	$0x20, %rsp
       5: 48 89 ce                     	movq	%rcx, %rsi
       8: e8 00 00 00 00               	callq	0xd <std::length_error::length_error[abi:nqe220108](char const*)+0xd>
       d: 48 8b 05 00 00 00 00         	movq	(%rip), %rax            # 0x14 <std::length_error::length_error[abi:nqe220108](char const*)+0x14>
      14: 48 83 c0 10                  	addq	$0x10, %rax
      18: 48 89 06                     	movq	%rax, (%rsi)
      1b: 48 83 c4 20                  	addq	$0x20, %rsp
      1f: 5e                           	popq	%rsi
      20: c3                           	retq

Disassembly of section .text$_ZSt28__throw_bad_array_new_lengthB9nqe220108v:

0000000000000000 <std::__throw_bad_array_new_length[abi:nqe220108]()>:
       0: 56                           	pushq	%rsi
       1: 48 83 ec 20                  	subq	$0x20, %rsp
       5: b9 08 00 00 00               	movl	$0x8, %ecx
       a: e8 00 00 00 00               	callq	0xf <std::__throw_bad_array_new_length[abi:nqe220108]()+0xf>
       f: 48 89 c6                     	movq	%rax, %rsi
      12: 48 89 c1                     	movq	%rax, %rcx
      15: e8 00 00 00 00               	callq	0x1a <std::__throw_bad_array_new_length[abi:nqe220108]()+0x1a>
      1a: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x21 <std::__throw_bad_array_new_length[abi:nqe220108]()+0x21>
      21: 4c 8d 05 00 00 00 00         	leaq	(%rip), %r8             # 0x28 <std::__throw_bad_array_new_length[abi:nqe220108]()+0x28>
      28: 48 89 f1                     	movq	%rsi, %rcx
      2b: e8 00 00 00 00               	callq	0x30 <std::__throw_bad_array_new_length[abi:nqe220108]()+0x30>
      30: cc                           	int3

Disassembly of section .text$_ZNSt3__16vectorIdNS_3pmr21polymorphic_allocatorIdEEE20__throw_length_errorB9nqe220108Ev:

0000000000000000 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::__throw_length_error[abi:nqe220108]()>:
       0: 48 83 ec 28                  	subq	$0x28, %rsp
       4: 48 8d 0d 48 00 00 00         	leaq	0x48(%rip), %rcx        # 0x53 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::__throw_length_error[abi:nqe220108]()+0x53>
       b: e8 00 00 00 00               	callq	0x10 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::__throw_length_error[abi:nqe220108]()+0x10>
      10: cc                           	int3
