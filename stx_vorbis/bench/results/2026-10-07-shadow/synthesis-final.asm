
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

0000000000000cb0 <stx_vorbis::detail::avx2_first_stage(double*, double*, double const*, double const*, unsigned long long)>:
     cb0: 48 83 ec 28                  	subq	$0x28, %rsp
     cb4: c5 f9 29 7c 24 10            	vmovapd	%xmm7, 0x10(%rsp)
     cba: c5 f9 29 34 24               	vmovapd	%xmm6, (%rsp)
     cbf: 4c 8b 54 24 50               	movq	0x50(%rsp), %r10
     cc4: 4c 89 d0                     	movq	%r10, %rax
     cc7: 48 83 e0 f8                  	andq	$-0x8, %rax
     ccb: 0f 84 98 00 00 00            	je	0xd69 <stx_vorbis::detail::avx2_first_stage(double*, double*, double const*, double const*, unsigned long long)+0xb9>
     cd1: c4 c2 7d 19 00               	vbroadcastsd	(%r8), %ymm0
     cd6: c4 c2 7d 19 09               	vbroadcastsd	(%r9), %ymm1
     cdb: 45 31 db                     	xorl	%r11d, %r11d
     cde: 66 90                        	nop
     ce0: c4 a1 7d 10 14 d9            	vmovupd	(%rcx,%r11,8), %ymm2
     ce6: c4 a1 7d 10 5c d9 20         	vmovupd	0x20(%rcx,%r11,8), %ymm3
     ced: c4 a1 7d 10 24 da            	vmovupd	(%rdx,%r11,8), %ymm4
     cf3: c4 a1 7d 10 6c da 20         	vmovupd	0x20(%rdx,%r11,8), %ymm5
     cfa: c5 ed 14 f3                  	vunpcklpd	%ymm3, %ymm2, %ymm6 # ymm6 = ymm2[0],ymm3[0],ymm2[2],ymm3[2]
     cfe: c5 ed 15 d3                  	vunpckhpd	%ymm3, %ymm2, %ymm2 # ymm2 = ymm2[1],ymm3[1],ymm2[3],ymm3[3]
     d02: c5 dd 14 dd                  	vunpcklpd	%ymm5, %ymm4, %ymm3 # ymm3 = ymm4[0],ymm5[0],ymm4[2],ymm5[2]
     d06: c5 dd 15 e5                  	vunpckhpd	%ymm5, %ymm4, %ymm4 # ymm4 = ymm4[1],ymm5[1],ymm4[3],ymm5[3]
     d0a: c5 fd 59 ea                  	vmulpd	%ymm2, %ymm0, %ymm5
     d0e: c5 f5 59 fc                  	vmulpd	%ymm4, %ymm1, %ymm7
     d12: c5 d5 5c ef                  	vsubpd	%ymm7, %ymm5, %ymm5
     d16: c5 f5 59 d2                  	vmulpd	%ymm2, %ymm1, %ymm2
     d1a: c5 fd 59 e4                  	vmulpd	%ymm4, %ymm0, %ymm4
     d1e: c5 ed 58 d4                  	vaddpd	%ymm4, %ymm2, %ymm2
     d22: c5 cd 58 e5                  	vaddpd	%ymm5, %ymm6, %ymm4
     d26: c5 cd 5c ed                  	vsubpd	%ymm5, %ymm6, %ymm5
     d2a: c5 e5 58 f2                  	vaddpd	%ymm2, %ymm3, %ymm6
     d2e: c5 e5 5c d2                  	vsubpd	%ymm2, %ymm3, %ymm2
     d32: c5 dd 14 dd                  	vunpcklpd	%ymm5, %ymm4, %ymm3 # ymm3 = ymm4[0],ymm5[0],ymm4[2],ymm5[2]
     d36: c4 a1 7d 11 1c d9            	vmovupd	%ymm3, (%rcx,%r11,8)
     d3c: c5 dd 15 dd                  	vunpckhpd	%ymm5, %ymm4, %ymm3 # ymm3 = ymm4[1],ymm5[1],ymm4[3],ymm5[3]
     d40: c4 a1 7d 11 5c d9 20         	vmovupd	%ymm3, 0x20(%rcx,%r11,8)
     d47: c5 cd 14 da                  	vunpcklpd	%ymm2, %ymm6, %ymm3 # ymm3 = ymm6[0],ymm2[0],ymm6[2],ymm2[2]
     d4b: c4 a1 7d 11 1c da            	vmovupd	%ymm3, (%rdx,%r11,8)
     d51: c5 cd 15 d2                  	vunpckhpd	%ymm2, %ymm6, %ymm2 # ymm2 = ymm6[1],ymm2[1],ymm6[3],ymm2[3]
     d55: c4 a1 7d 11 54 da 20         	vmovupd	%ymm2, 0x20(%rdx,%r11,8)
     d5c: 49 83 c3 08                  	addq	$0x8, %r11
     d60: 49 39 c3                     	cmpq	%rax, %r11
     d63: 0f 82 77 ff ff ff            	jb	0xce0 <stx_vorbis::detail::avx2_first_stage(double*, double*, double const*, double const*, unsigned long long)+0x30>
     d69: 4c 39 d0                     	cmpq	%r10, %rax
     d6c: 0f 84 a8 01 00 00            	je	0xf1a <stx_vorbis::detail::avx2_first_stage(double*, double*, double const*, double const*, unsigned long long)+0x26a>
     d72: 41 83 e2 07                  	andl	$0x7, %r10d
     d76: 0f 84 9e 01 00 00            	je	0xf1a <stx_vorbis::detail::avx2_first_stage(double*, double*, double const*, double const*, unsigned long long)+0x26a>
     d7c: c4 c1 7b 10 00               	vmovsd	(%r8), %xmm0
     d81: c4 c1 7b 10 09               	vmovsd	(%r9), %xmm1
     d86: c5 fb 10 14 c1               	vmovsd	(%rcx,%rax,8), %xmm2
     d8b: c5 fb 10 5c c1 08            	vmovsd	0x8(%rcx,%rax,8), %xmm3
     d91: c5 fb 59 e3                  	vmulsd	%xmm3, %xmm0, %xmm4
     d95: c5 fb 10 2c c2               	vmovsd	(%rdx,%rax,8), %xmm5
     d9a: c5 fb 10 74 c2 08            	vmovsd	0x8(%rdx,%rax,8), %xmm6
     da0: c5 f3 59 fe                  	vmulsd	%xmm6, %xmm1, %xmm7
     da4: c5 db 5c e7                  	vsubsd	%xmm7, %xmm4, %xmm4
     da8: c5 f3 59 cb                  	vmulsd	%xmm3, %xmm1, %xmm1
     dac: c5 fb 59 c6                  	vmulsd	%xmm6, %xmm0, %xmm0
     db0: c5 f3 58 c0                  	vaddsd	%xmm0, %xmm1, %xmm0
     db4: c5 eb 58 cc                  	vaddsd	%xmm4, %xmm2, %xmm1
     db8: c5 fb 11 0c c1               	vmovsd	%xmm1, (%rcx,%rax,8)
     dbd: c5 d3 58 c8                  	vaddsd	%xmm0, %xmm5, %xmm1
     dc1: c5 fb 11 0c c2               	vmovsd	%xmm1, (%rdx,%rax,8)
     dc6: c5 eb 5c cc                  	vsubsd	%xmm4, %xmm2, %xmm1
     dca: c5 fb 11 4c c1 08            	vmovsd	%xmm1, 0x8(%rcx,%rax,8)
     dd0: c5 d3 5c c0                  	vsubsd	%xmm0, %xmm5, %xmm0
     dd4: c5 fb 11 44 c2 08            	vmovsd	%xmm0, 0x8(%rdx,%rax,8)
     dda: 41 83 fa 03                  	cmpl	$0x3, %r10d
     dde: 0f 82 36 01 00 00            	jb	0xf1a <stx_vorbis::detail::avx2_first_stage(double*, double*, double const*, double const*, unsigned long long)+0x26a>
     de4: c4 c1 7b 10 00               	vmovsd	(%r8), %xmm0
     de9: c4 c1 7b 10 09               	vmovsd	(%r9), %xmm1
     dee: c5 fb 10 54 c1 10            	vmovsd	0x10(%rcx,%rax,8), %xmm2
     df4: c5 fb 10 5c c1 18            	vmovsd	0x18(%rcx,%rax,8), %xmm3
     dfa: c5 fb 59 e3                  	vmulsd	%xmm3, %xmm0, %xmm4
     dfe: c5 fb 10 6c c2 10            	vmovsd	0x10(%rdx,%rax,8), %xmm5
     e04: c5 fb 10 74 c2 18            	vmovsd	0x18(%rdx,%rax,8), %xmm6
     e0a: c5 f3 59 fe                  	vmulsd	%xmm6, %xmm1, %xmm7
     e0e: c5 db 5c e7                  	vsubsd	%xmm7, %xmm4, %xmm4
     e12: c5 f3 59 cb                  	vmulsd	%xmm3, %xmm1, %xmm1
     e16: c5 fb 59 c6                  	vmulsd	%xmm6, %xmm0, %xmm0
     e1a: c5 f3 58 c0                  	vaddsd	%xmm0, %xmm1, %xmm0
     e1e: c5 eb 58 cc                  	vaddsd	%xmm4, %xmm2, %xmm1
     e22: c5 fb 11 4c c1 10            	vmovsd	%xmm1, 0x10(%rcx,%rax,8)
     e28: c5 d3 58 c8                  	vaddsd	%xmm0, %xmm5, %xmm1
     e2c: c5 fb 11 4c c2 10            	vmovsd	%xmm1, 0x10(%rdx,%rax,8)
     e32: c5 eb 5c cc                  	vsubsd	%xmm4, %xmm2, %xmm1
     e36: c5 fb 11 4c c1 18            	vmovsd	%xmm1, 0x18(%rcx,%rax,8)
     e3c: c5 d3 5c c0                  	vsubsd	%xmm0, %xmm5, %xmm0
     e40: c5 fb 11 44 c2 18            	vmovsd	%xmm0, 0x18(%rdx,%rax,8)
     e46: 41 83 fa 05                  	cmpl	$0x5, %r10d
     e4a: 0f 82 ca 00 00 00            	jb	0xf1a <stx_vorbis::detail::avx2_first_stage(double*, double*, double const*, double const*, unsigned long long)+0x26a>
     e50: c4 c1 7b 10 00               	vmovsd	(%r8), %xmm0
     e55: c4 c1 7b 10 09               	vmovsd	(%r9), %xmm1
     e5a: c5 fb 10 54 c1 20            	vmovsd	0x20(%rcx,%rax,8), %xmm2
     e60: c5 fb 10 5c c1 28            	vmovsd	0x28(%rcx,%rax,8), %xmm3
     e66: c5 fb 59 e3                  	vmulsd	%xmm3, %xmm0, %xmm4
     e6a: c5 fb 10 6c c2 20            	vmovsd	0x20(%rdx,%rax,8), %xmm5
     e70: c5 fb 10 74 c2 28            	vmovsd	0x28(%rdx,%rax,8), %xmm6
     e76: c5 f3 59 fe                  	vmulsd	%xmm6, %xmm1, %xmm7
     e7a: c5 db 5c e7                  	vsubsd	%xmm7, %xmm4, %xmm4
     e7e: c5 f3 59 cb                  	vmulsd	%xmm3, %xmm1, %xmm1
     e82: c5 fb 59 c6                  	vmulsd	%xmm6, %xmm0, %xmm0
     e86: c5 f3 58 c0                  	vaddsd	%xmm0, %xmm1, %xmm0
     e8a: c5 eb 58 cc                  	vaddsd	%xmm4, %xmm2, %xmm1
     e8e: c5 fb 11 4c c1 20            	vmovsd	%xmm1, 0x20(%rcx,%rax,8)
     e94: c5 d3 58 c8                  	vaddsd	%xmm0, %xmm5, %xmm1
     e98: c5 fb 11 4c c2 20            	vmovsd	%xmm1, 0x20(%rdx,%rax,8)
     e9e: c5 eb 5c cc                  	vsubsd	%xmm4, %xmm2, %xmm1
     ea2: c5 fb 11 4c c1 28            	vmovsd	%xmm1, 0x28(%rcx,%rax,8)
     ea8: c5 d3 5c c0                  	vsubsd	%xmm0, %xmm5, %xmm0
     eac: c5 fb 11 44 c2 28            	vmovsd	%xmm0, 0x28(%rdx,%rax,8)
     eb2: 41 83 fa 07                  	cmpl	$0x7, %r10d
     eb6: 75 62                        	jne	0xf1a <stx_vorbis::detail::avx2_first_stage(double*, double*, double const*, double const*, unsigned long long)+0x26a>
     eb8: c4 c1 7b 10 00               	vmovsd	(%r8), %xmm0
     ebd: c4 c1 7b 10 09               	vmovsd	(%r9), %xmm1
     ec2: c5 fb 10 54 c1 30            	vmovsd	0x30(%rcx,%rax,8), %xmm2
     ec8: c5 fb 10 5c c1 38            	vmovsd	0x38(%rcx,%rax,8), %xmm3
     ece: c5 fb 59 e3                  	vmulsd	%xmm3, %xmm0, %xmm4
     ed2: c5 fb 10 6c c2 30            	vmovsd	0x30(%rdx,%rax,8), %xmm5
     ed8: c5 fb 10 74 c2 38            	vmovsd	0x38(%rdx,%rax,8), %xmm6
     ede: c5 f3 59 fe                  	vmulsd	%xmm6, %xmm1, %xmm7
     ee2: c5 db 5c e7                  	vsubsd	%xmm7, %xmm4, %xmm4
     ee6: c5 f3 59 cb                  	vmulsd	%xmm3, %xmm1, %xmm1
     eea: c5 fb 59 c6                  	vmulsd	%xmm6, %xmm0, %xmm0
     eee: c5 f3 58 c0                  	vaddsd	%xmm0, %xmm1, %xmm0
     ef2: c5 eb 58 cc                  	vaddsd	%xmm4, %xmm2, %xmm1
     ef6: c5 fb 11 4c c1 30            	vmovsd	%xmm1, 0x30(%rcx,%rax,8)
     efc: c5 d3 58 c8                  	vaddsd	%xmm0, %xmm5, %xmm1
     f00: c5 fb 11 4c c2 30            	vmovsd	%xmm1, 0x30(%rdx,%rax,8)
     f06: c5 eb 5c cc                  	vsubsd	%xmm4, %xmm2, %xmm1
     f0a: c5 fb 11 4c c1 38            	vmovsd	%xmm1, 0x38(%rcx,%rax,8)
     f10: c5 d3 5c c0                  	vsubsd	%xmm0, %xmm5, %xmm0
     f14: c5 fb 11 44 c2 38            	vmovsd	%xmm0, 0x38(%rdx,%rax,8)
     f1a: c5 f8 28 34 24               	vmovaps	(%rsp), %xmm6
     f1f: c5 f8 28 7c 24 10            	vmovaps	0x10(%rsp), %xmm7
     f25: 48 83 c4 28                  	addq	$0x28, %rsp
     f29: c5 f8 77                     	vzeroupper
     f2c: c3                           	retq
     f2d: 0f 1f 00                     	nopl	(%rax)

0000000000000f30 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)>:
     f30: 41 57                        	pushq	%r15
     f32: 41 56                        	pushq	%r14
     f34: 56                           	pushq	%rsi
     f35: 57                           	pushq	%rdi
     f36: 53                           	pushq	%rbx
     f37: 48 83 ec 20                  	subq	$0x20, %rsp
     f3b: c5 f9 29 7c 24 10            	vmovapd	%xmm7, 0x10(%rsp)
     f41: c5 f9 29 34 24               	vmovapd	%xmm6, (%rsp)
     f46: 48 8b 44 24 78               	movq	0x78(%rsp), %rax
     f4b: 4c 8b 54 24 70               	movq	0x70(%rsp), %r10
     f50: 49 83 fa 01                  	cmpq	$0x1, %r10
     f54: 0f 85 85 02 00 00            	jne	0x11df <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x2af>
     f5a: 49 89 c2                     	movq	%rax, %r10
     f5d: 49 83 e2 f8                  	andq	$-0x8, %r10
     f61: 0f 84 a2 00 00 00            	je	0x1009 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0xd9>
     f67: c4 c2 7d 19 00               	vbroadcastsd	(%r8), %ymm0
     f6c: c4 c2 7d 19 09               	vbroadcastsd	(%r9), %ymm1
     f71: 45 31 db                     	xorl	%r11d, %r11d
     f74: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
     f80: c4 a1 7d 10 14 d9            	vmovupd	(%rcx,%r11,8), %ymm2
     f86: c4 a1 7d 10 5c d9 20         	vmovupd	0x20(%rcx,%r11,8), %ymm3
     f8d: c4 a1 7d 10 24 da            	vmovupd	(%rdx,%r11,8), %ymm4
     f93: c4 a1 7d 10 6c da 20         	vmovupd	0x20(%rdx,%r11,8), %ymm5
     f9a: c5 ed 14 f3                  	vunpcklpd	%ymm3, %ymm2, %ymm6 # ymm6 = ymm2[0],ymm3[0],ymm2[2],ymm3[2]
     f9e: c5 ed 15 d3                  	vunpckhpd	%ymm3, %ymm2, %ymm2 # ymm2 = ymm2[1],ymm3[1],ymm2[3],ymm3[3]
     fa2: c5 dd 14 dd                  	vunpcklpd	%ymm5, %ymm4, %ymm3 # ymm3 = ymm4[0],ymm5[0],ymm4[2],ymm5[2]
     fa6: c5 dd 15 e5                  	vunpckhpd	%ymm5, %ymm4, %ymm4 # ymm4 = ymm4[1],ymm5[1],ymm4[3],ymm5[3]
     faa: c5 fd 59 ea                  	vmulpd	%ymm2, %ymm0, %ymm5
     fae: c5 f5 59 fc                  	vmulpd	%ymm4, %ymm1, %ymm7
     fb2: c5 d5 5c ef                  	vsubpd	%ymm7, %ymm5, %ymm5
     fb6: c5 f5 59 d2                  	vmulpd	%ymm2, %ymm1, %ymm2
     fba: c5 fd 59 e4                  	vmulpd	%ymm4, %ymm0, %ymm4
     fbe: c5 ed 58 d4                  	vaddpd	%ymm4, %ymm2, %ymm2
     fc2: c5 cd 58 e5                  	vaddpd	%ymm5, %ymm6, %ymm4
     fc6: c5 cd 5c ed                  	vsubpd	%ymm5, %ymm6, %ymm5
     fca: c5 e5 58 f2                  	vaddpd	%ymm2, %ymm3, %ymm6
     fce: c5 e5 5c d2                  	vsubpd	%ymm2, %ymm3, %ymm2
     fd2: c5 dd 14 dd                  	vunpcklpd	%ymm5, %ymm4, %ymm3 # ymm3 = ymm4[0],ymm5[0],ymm4[2],ymm5[2]
     fd6: c4 a1 7d 11 1c d9            	vmovupd	%ymm3, (%rcx,%r11,8)
     fdc: c5 dd 15 dd                  	vunpckhpd	%ymm5, %ymm4, %ymm3 # ymm3 = ymm4[1],ymm5[1],ymm4[3],ymm5[3]
     fe0: c4 a1 7d 11 5c d9 20         	vmovupd	%ymm3, 0x20(%rcx,%r11,8)
     fe7: c5 cd 14 da                  	vunpcklpd	%ymm2, %ymm6, %ymm3 # ymm3 = ymm6[0],ymm2[0],ymm6[2],ymm2[2]
     feb: c4 a1 7d 11 1c da            	vmovupd	%ymm3, (%rdx,%r11,8)
     ff1: c5 cd 15 d2                  	vunpckhpd	%ymm2, %ymm6, %ymm2 # ymm2 = ymm6[1],ymm2[1],ymm6[3],ymm2[3]
     ff5: c4 a1 7d 11 54 da 20         	vmovupd	%ymm2, 0x20(%rdx,%r11,8)
     ffc: 49 83 c3 08                  	addq	$0x8, %r11
    1000: 4d 39 d3                     	cmpq	%r10, %r11
    1003: 0f 82 77 ff ff ff            	jb	0xf80 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x50>
    1009: 49 39 c2                     	cmpq	%rax, %r10
    100c: 0f 84 53 03 00 00            	je	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    1012: 83 e0 07                     	andl	$0x7, %eax
    1015: 0f 84 4a 03 00 00            	je	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    101b: c4 c1 7b 10 00               	vmovsd	(%r8), %xmm0
    1020: c4 c1 7b 10 09               	vmovsd	(%r9), %xmm1
    1025: c4 a1 7b 10 14 d1            	vmovsd	(%rcx,%r10,8), %xmm2
    102b: c4 a1 7b 10 5c d1 08         	vmovsd	0x8(%rcx,%r10,8), %xmm3
    1032: c5 fb 59 e3                  	vmulsd	%xmm3, %xmm0, %xmm4
    1036: c4 a1 7b 10 2c d2            	vmovsd	(%rdx,%r10,8), %xmm5
    103c: c4 a1 7b 10 74 d2 08         	vmovsd	0x8(%rdx,%r10,8), %xmm6
    1043: c5 f3 59 fe                  	vmulsd	%xmm6, %xmm1, %xmm7
    1047: c5 db 5c e7                  	vsubsd	%xmm7, %xmm4, %xmm4
    104b: c5 f3 59 cb                  	vmulsd	%xmm3, %xmm1, %xmm1
    104f: c5 fb 59 c6                  	vmulsd	%xmm6, %xmm0, %xmm0
    1053: c5 f3 58 c0                  	vaddsd	%xmm0, %xmm1, %xmm0
    1057: c5 eb 58 cc                  	vaddsd	%xmm4, %xmm2, %xmm1
    105b: c4 a1 7b 11 0c d1            	vmovsd	%xmm1, (%rcx,%r10,8)
    1061: c5 d3 58 c8                  	vaddsd	%xmm0, %xmm5, %xmm1
    1065: c4 a1 7b 11 0c d2            	vmovsd	%xmm1, (%rdx,%r10,8)
    106b: c5 eb 5c cc                  	vsubsd	%xmm4, %xmm2, %xmm1
    106f: c4 a1 7b 11 4c d1 08         	vmovsd	%xmm1, 0x8(%rcx,%r10,8)
    1076: c5 d3 5c c0                  	vsubsd	%xmm0, %xmm5, %xmm0
    107a: c4 a1 7b 11 44 d2 08         	vmovsd	%xmm0, 0x8(%rdx,%r10,8)
    1081: 83 f8 03                     	cmpl	$0x3, %eax
    1084: 0f 82 db 02 00 00            	jb	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    108a: c4 c1 7b 10 00               	vmovsd	(%r8), %xmm0
    108f: c4 c1 7b 10 09               	vmovsd	(%r9), %xmm1
    1094: c4 a1 7b 10 54 d1 10         	vmovsd	0x10(%rcx,%r10,8), %xmm2
    109b: c4 a1 7b 10 5c d1 18         	vmovsd	0x18(%rcx,%r10,8), %xmm3
    10a2: c5 fb 59 e3                  	vmulsd	%xmm3, %xmm0, %xmm4
    10a6: c4 a1 7b 10 6c d2 10         	vmovsd	0x10(%rdx,%r10,8), %xmm5
    10ad: c4 a1 7b 10 74 d2 18         	vmovsd	0x18(%rdx,%r10,8), %xmm6
    10b4: c5 f3 59 fe                  	vmulsd	%xmm6, %xmm1, %xmm7
    10b8: c5 db 5c e7                  	vsubsd	%xmm7, %xmm4, %xmm4
    10bc: c5 f3 59 cb                  	vmulsd	%xmm3, %xmm1, %xmm1
    10c0: c5 fb 59 c6                  	vmulsd	%xmm6, %xmm0, %xmm0
    10c4: c5 f3 58 c0                  	vaddsd	%xmm0, %xmm1, %xmm0
    10c8: c5 eb 58 cc                  	vaddsd	%xmm4, %xmm2, %xmm1
    10cc: c4 a1 7b 11 4c d1 10         	vmovsd	%xmm1, 0x10(%rcx,%r10,8)
    10d3: c5 d3 58 c8                  	vaddsd	%xmm0, %xmm5, %xmm1
    10d7: c4 a1 7b 11 4c d2 10         	vmovsd	%xmm1, 0x10(%rdx,%r10,8)
    10de: c5 eb 5c cc                  	vsubsd	%xmm4, %xmm2, %xmm1
    10e2: c4 a1 7b 11 4c d1 18         	vmovsd	%xmm1, 0x18(%rcx,%r10,8)
    10e9: c5 d3 5c c0                  	vsubsd	%xmm0, %xmm5, %xmm0
    10ed: c4 a1 7b 11 44 d2 18         	vmovsd	%xmm0, 0x18(%rdx,%r10,8)
    10f4: 83 f8 05                     	cmpl	$0x5, %eax
    10f7: 0f 82 68 02 00 00            	jb	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    10fd: c4 c1 7b 10 00               	vmovsd	(%r8), %xmm0
    1102: c4 c1 7b 10 09               	vmovsd	(%r9), %xmm1
    1107: c4 a1 7b 10 54 d1 20         	vmovsd	0x20(%rcx,%r10,8), %xmm2
    110e: c4 a1 7b 10 5c d1 28         	vmovsd	0x28(%rcx,%r10,8), %xmm3
    1115: c5 fb 59 e3                  	vmulsd	%xmm3, %xmm0, %xmm4
    1119: c4 a1 7b 10 6c d2 20         	vmovsd	0x20(%rdx,%r10,8), %xmm5
    1120: c4 a1 7b 10 74 d2 28         	vmovsd	0x28(%rdx,%r10,8), %xmm6
    1127: c5 f3 59 fe                  	vmulsd	%xmm6, %xmm1, %xmm7
    112b: c5 db 5c e7                  	vsubsd	%xmm7, %xmm4, %xmm4
    112f: c5 f3 59 cb                  	vmulsd	%xmm3, %xmm1, %xmm1
    1133: c5 fb 59 c6                  	vmulsd	%xmm6, %xmm0, %xmm0
    1137: c5 f3 58 c0                  	vaddsd	%xmm0, %xmm1, %xmm0
    113b: c5 eb 58 cc                  	vaddsd	%xmm4, %xmm2, %xmm1
    113f: c4 a1 7b 11 4c d1 20         	vmovsd	%xmm1, 0x20(%rcx,%r10,8)
    1146: c5 d3 58 c8                  	vaddsd	%xmm0, %xmm5, %xmm1
    114a: c4 a1 7b 11 4c d2 20         	vmovsd	%xmm1, 0x20(%rdx,%r10,8)
    1151: c5 eb 5c cc                  	vsubsd	%xmm4, %xmm2, %xmm1
    1155: c4 a1 7b 11 4c d1 28         	vmovsd	%xmm1, 0x28(%rcx,%r10,8)
    115c: c5 d3 5c c0                  	vsubsd	%xmm0, %xmm5, %xmm0
    1160: c4 a1 7b 11 44 d2 28         	vmovsd	%xmm0, 0x28(%rdx,%r10,8)
    1167: 83 f8 07                     	cmpl	$0x7, %eax
    116a: 0f 85 f5 01 00 00            	jne	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    1170: c4 c1 7b 10 00               	vmovsd	(%r8), %xmm0
    1175: c4 c1 7b 10 09               	vmovsd	(%r9), %xmm1
    117a: c4 a1 7b 10 54 d1 30         	vmovsd	0x30(%rcx,%r10,8), %xmm2
    1181: c4 a1 7b 10 5c d1 38         	vmovsd	0x38(%rcx,%r10,8), %xmm3
    1188: c5 fb 59 e3                  	vmulsd	%xmm3, %xmm0, %xmm4
    118c: c4 a1 7b 10 6c d2 30         	vmovsd	0x30(%rdx,%r10,8), %xmm5
    1193: c4 a1 7b 10 74 d2 38         	vmovsd	0x38(%rdx,%r10,8), %xmm6
    119a: c5 f3 59 fe                  	vmulsd	%xmm6, %xmm1, %xmm7
    119e: c5 db 5c e7                  	vsubsd	%xmm7, %xmm4, %xmm4
    11a2: c5 f3 59 cb                  	vmulsd	%xmm3, %xmm1, %xmm1
    11a6: c5 fb 59 c6                  	vmulsd	%xmm6, %xmm0, %xmm0
    11aa: c5 f3 58 c0                  	vaddsd	%xmm0, %xmm1, %xmm0
    11ae: c5 eb 58 cc                  	vaddsd	%xmm4, %xmm2, %xmm1
    11b2: c4 a1 7b 11 4c d1 30         	vmovsd	%xmm1, 0x30(%rcx,%r10,8)
    11b9: c5 d3 58 c8                  	vaddsd	%xmm0, %xmm5, %xmm1
    11bd: c4 a1 7b 11 4c d2 30         	vmovsd	%xmm1, 0x30(%rdx,%r10,8)
    11c4: c5 eb 5c cc                  	vsubsd	%xmm4, %xmm2, %xmm1
    11c8: c4 a1 7b 11 4c d1 38         	vmovsd	%xmm1, 0x38(%rcx,%r10,8)
    11cf: c5 d3 5c c0                  	vsubsd	%xmm0, %xmm5, %xmm0
    11d3: c4 a1 7b 11 44 d2 38         	vmovsd	%xmm0, 0x38(%rdx,%r10,8)
    11da: e9 86 01 00 00               	jmp	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    11df: 49 83 fa 04                  	cmpq	$0x4, %r10
    11e3: 0f 83 b9 00 00 00            	jae	0x12a2 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x372>
    11e9: 4d 85 d2                     	testq	%r10, %r10
    11ec: 0f 84 73 01 00 00            	je	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    11f2: 48 85 c0                     	testq	%rax, %rax
    11f5: 0f 84 6a 01 00 00            	je	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    11fb: 4f 8d 1c 12                  	leaq	(%r10,%r10), %r11
    11ff: 31 f6                        	xorl	%esi, %esi
    1201: 66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00 	nopw	%cs:(%rax,%rax)
    1210: 31 ff                        	xorl	%edi, %edi
    1212: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
    1220: 48 8d 1c 37                  	leaq	(%rdi,%rsi), %rbx
    1224: 4e 8d 34 13                  	leaq	(%rbx,%r10), %r14
    1228: c4 c1 79 10 04 f8            	vmovupd	(%r8,%rdi,8), %xmm0
    122e: c4 c1 79 10 0c f9            	vmovupd	(%r9,%rdi,8), %xmm1
    1234: c4 a1 79 10 14 f1            	vmovupd	(%rcx,%r14,8), %xmm2
    123a: c4 a1 79 10 1c f2            	vmovupd	(%rdx,%r14,8), %xmm3
    1240: c5 f9 59 e2                  	vmulpd	%xmm2, %xmm0, %xmm4
    1244: c5 f1 59 eb                  	vmulpd	%xmm3, %xmm1, %xmm5
    1248: c5 d9 5c e5                  	vsubpd	%xmm5, %xmm4, %xmm4
    124c: c5 f1 59 ca                  	vmulpd	%xmm2, %xmm1, %xmm1
    1250: c5 f9 59 c3                  	vmulpd	%xmm3, %xmm0, %xmm0
    1254: c5 f1 58 c0                  	vaddpd	%xmm0, %xmm1, %xmm0
    1258: c5 f9 10 0c d9               	vmovupd	(%rcx,%rbx,8), %xmm1
    125d: c5 f9 10 14 da               	vmovupd	(%rdx,%rbx,8), %xmm2
    1262: c5 f1 58 dc                  	vaddpd	%xmm4, %xmm1, %xmm3
    1266: c5 f9 11 1c d9               	vmovupd	%xmm3, (%rcx,%rbx,8)
    126b: c5 e9 58 d8                  	vaddpd	%xmm0, %xmm2, %xmm3
    126f: c5 f9 11 1c da               	vmovupd	%xmm3, (%rdx,%rbx,8)
    1274: c5 f1 5c cc                  	vsubpd	%xmm4, %xmm1, %xmm1
    1278: c4 a1 79 11 0c f1            	vmovupd	%xmm1, (%rcx,%r14,8)
    127e: c5 e9 5c c0                  	vsubpd	%xmm0, %xmm2, %xmm0
    1282: c4 a1 79 11 04 f2            	vmovupd	%xmm0, (%rdx,%r14,8)
    1288: 48 83 c7 02                  	addq	$0x2, %rdi
    128c: 4c 39 d7                     	cmpq	%r10, %rdi
    128f: 72 8f                        	jb	0x1220 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x2f0>
    1291: 4c 01 de                     	addq	%r11, %rsi
    1294: 48 39 c6                     	cmpq	%rax, %rsi
    1297: 0f 82 73 ff ff ff            	jb	0x1210 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x2e0>
    129d: e9 c3 00 00 00               	jmp	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    12a2: 48 85 c0                     	testq	%rax, %rax
    12a5: 0f 84 ba 00 00 00            	je	0x1365 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x435>
    12ab: 4f 8d 1c 12                  	leaq	(%r10,%r10), %r11
    12af: 4a 8d 34 d1                  	leaq	(%rcx,%r10,8), %rsi
    12b3: 4c 89 d7                     	movq	%r10, %rdi
    12b6: 48 c1 e7 04                  	shlq	$0x4, %rdi
    12ba: 4a 8d 1c d2                  	leaq	(%rdx,%r10,8), %rbx
    12be: 45 31 f6                     	xorl	%r14d, %r14d
    12c1: 66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00 	nopw	%cs:(%rax,%rax)
    12d0: 45 31 ff                     	xorl	%r15d, %r15d
    12d3: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
    12e0: c4 81 7d 10 04 f8            	vmovupd	(%r8,%r15,8), %ymm0
    12e6: c4 81 7d 10 0c f9            	vmovupd	(%r9,%r15,8), %ymm1
    12ec: c4 a1 7d 10 14 fe            	vmovupd	(%rsi,%r15,8), %ymm2
    12f2: c4 a1 7d 10 1c fb            	vmovupd	(%rbx,%r15,8), %ymm3
    12f8: c5 fd 59 e2                  	vmulpd	%ymm2, %ymm0, %ymm4
    12fc: c5 f5 59 eb                  	vmulpd	%ymm3, %ymm1, %ymm5
    1300: c5 dd 5c e5                  	vsubpd	%ymm5, %ymm4, %ymm4
    1304: c5 f5 59 ca                  	vmulpd	%ymm2, %ymm1, %ymm1
    1308: c5 fd 59 c3                  	vmulpd	%ymm3, %ymm0, %ymm0
    130c: c5 f5 58 c0                  	vaddpd	%ymm0, %ymm1, %ymm0
    1310: c4 a1 7d 10 0c f9            	vmovupd	(%rcx,%r15,8), %ymm1
    1316: c4 a1 7d 10 14 fa            	vmovupd	(%rdx,%r15,8), %ymm2
    131c: c5 f5 58 dc                  	vaddpd	%ymm4, %ymm1, %ymm3
    1320: c4 a1 7d 11 1c f9            	vmovupd	%ymm3, (%rcx,%r15,8)
    1326: c5 ed 58 d8                  	vaddpd	%ymm0, %ymm2, %ymm3
    132a: c4 a1 7d 11 1c fa            	vmovupd	%ymm3, (%rdx,%r15,8)
    1330: c5 f5 5c cc                  	vsubpd	%ymm4, %ymm1, %ymm1
    1334: c4 a1 7d 11 0c fe            	vmovupd	%ymm1, (%rsi,%r15,8)
    133a: c5 ed 5c c0                  	vsubpd	%ymm0, %ymm2, %ymm0
    133e: c4 a1 7d 11 04 fb            	vmovupd	%ymm0, (%rbx,%r15,8)
    1344: 49 83 c7 04                  	addq	$0x4, %r15
    1348: 4d 39 d7                     	cmpq	%r10, %r15
    134b: 72 93                        	jb	0x12e0 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x3b0>
    134d: 4d 01 de                     	addq	%r11, %r14
    1350: 48 01 fe                     	addq	%rdi, %rsi
    1353: 48 01 fb                     	addq	%rdi, %rbx
    1356: 48 01 f9                     	addq	%rdi, %rcx
    1359: 48 01 fa                     	addq	%rdi, %rdx
    135c: 49 39 c6                     	cmpq	%rax, %r14
    135f: 0f 82 6b ff ff ff            	jb	0x12d0 <stx_vorbis::detail::avx2_butterfly(double*, double*, double const*, double const*, unsigned long long, unsigned long long)+0x3a0>
    1365: c5 f8 28 34 24               	vmovaps	(%rsp), %xmm6
    136a: c5 f8 28 7c 24 10            	vmovaps	0x10(%rsp), %xmm7
    1370: 48 83 c4 20                  	addq	$0x20, %rsp
    1374: 5b                           	popq	%rbx
    1375: 5f                           	popq	%rdi
    1376: 5e                           	popq	%rsi
    1377: 41 5e                        	popq	%r14
    1379: 41 5f                        	popq	%r15
    137b: c5 f8 77                     	vzeroupper
    137e: c3                           	retq
    137f: 90                           	nop

0000000000001380 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)>:
    1380: 48 83 ec 28                  	subq	$0x28, %rsp
    1384: 80 f9 02                     	cmpb	$0x2, %cl
    1387: 73 10                        	jae	0x1399 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x19>
    1389: 84 c9                        	testb	%cl, %cl
    138b: 74 3a                        	je	0x13c7 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x47>
    138d: 48 8d 05 00 00 00 00         	leaq	(%rip), %rax            # 0x1394 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x14>
    1394: 48 83 c4 28                  	addq	$0x28, %rsp
    1398: c3                           	retq
    1399: 80 f9 03                     	cmpb	$0x3, %cl
    139c: 74 1d                        	je	0x13bb <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x3b>
    139e: 0f b6 c1                     	movzbl	%cl, %eax
    13a1: 83 f8 04                     	cmpl	$0x4, %eax
    13a4: 75 3f                        	jne	0x13e5 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x65>
    13a6: f6 05 0c 00 00 00 04         	testb	$0x4, 0xc(%rip)         # 0x13b9 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x39>
    13ad: 74 36                        	je	0x13e5 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x65>
    13af: 48 8d 05 00 00 00 00         	leaq	(%rip), %rax            # 0x13b6 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x36>
    13b6: 48 83 c4 28                  	addq	$0x28, %rsp
    13ba: c3                           	retq
    13bb: 48 8d 05 00 00 00 00         	leaq	(%rip), %rax            # 0x13c2 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x42>
    13c2: 48 83 c4 28                  	addq	$0x28, %rsp
    13c6: c3                           	retq
    13c7: f6 05 0c 00 00 00 04         	testb	$0x4, 0xc(%rip)         # 0x13da <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x5a>
    13ce: 48 8d 0d 00 00 00 00         	leaq	(%rip), %rcx            # 0x13d5 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x55>
    13d5: 48 8d 05 00 00 00 00         	leaq	(%rip), %rax            # 0x13dc <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x5c>
    13dc: 48 0f 44 c1                  	cmoveq	%rcx, %rax
    13e0: 48 83 c4 28                  	addq	$0x28, %rsp
    13e4: c3                           	retq
    13e5: b9 10 00 00 00               	movl	$0x10, %ecx
    13ea: e8 00 00 00 00               	callq	0x13ef <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x6f>
    13ef: c6 00 0f                     	movb	$0xf, (%rax)
    13f2: 48 c7 40 08 00 00 00 00      	movq	$0x0, 0x8(%rax)
    13fa: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x1401 <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x81>
    1401: 48 89 c1                     	movq	%rax, %rcx
    1404: 45 31 c0                     	xorl	%r8d, %r8d
    1407: e8 00 00 00 00               	callq	0x140c <stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis)+0x8c>
    140c: cc                           	int3
    140d: 0f 1f 00                     	nopl	(%rax)

0000000000001410 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))>:
    1410: 41 57                        	pushq	%r15
    1412: 41 56                        	pushq	%r14
    1414: 41 55                        	pushq	%r13
    1416: 41 54                        	pushq	%r12
    1418: 56                           	pushq	%rsi
    1419: 57                           	pushq	%rdi
    141a: 55                           	pushq	%rbp
    141b: 53                           	pushq	%rbx
    141c: 48 83 ec 38                  	subq	$0x38, %rsp
    1420: 4c 89 cf                     	movq	%r9, %rdi
    1423: 4c 89 44 24 30               	movq	%r8, 0x30(%rsp)
    1428: 49 89 d4                     	movq	%rdx, %r12
    142b: 48 89 cb                     	movq	%rcx, %rbx
    142e: 48 8b b4 24 a0 00 00 00      	movq	0xa0(%rsp), %rsi
    1436: 8b 69 04                     	movl	0x4(%rcx), %ebp
    1439: 4d 8b 31                     	movq	(%r9), %r14
    143c: 48 85 ed                     	testq	%rbp, %rbp
    143f: 74 34                        	je	0x1475 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x65>
    1441: 4c 8d 2c ed 00 00 00 00      	leaq	(,%rbp,8), %r13
    1449: 4c 89 f1                     	movq	%r14, %rcx
    144c: 31 d2                        	xorl	%edx, %edx
    144e: 4d 89 e8                     	movq	%r13, %r8
    1451: e8 00 00 00 00               	callq	0x1456 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x46>
    1456: 4c 8b 3e                     	movq	(%rsi), %r15
    1459: 4c 89 f9                     	movq	%r15, %rcx
    145c: 31 d2                        	xorl	%edx, %edx
    145e: 4d 89 e8                     	movq	%r13, %r8
    1461: e8 00 00 00 00               	callq	0x1466 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x56>
    1466: 8b 03                        	movl	(%rbx), %eax
    1468: 41 89 c3                     	movl	%eax, %r11d
    146b: 41 d1 eb                     	shrl	%r11d
    146e: 75 16                        	jne	0x1486 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x76>
    1470: e9 c7 00 00 00               	jmp	0x153c <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x12c>
    1475: 4c 8b 3e                     	movq	(%rsi), %r15
    1478: 8b 03                        	movl	(%rbx), %eax
    147a: 41 89 c3                     	movl	%eax, %r11d
    147d: 41 d1 eb                     	shrl	%r11d
    1480: 0f 84 b6 00 00 00            	je	0x153c <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x12c>
    1486: 4c 8b 4b 08                  	movq	0x8(%rbx), %r9
    148a: 4c 8b 43 68                  	movq	0x68(%rbx), %r8
    148e: 49 8b 14 24                  	movq	(%r12), %rdx
    1492: 48 8b 8b 88 00 00 00         	movq	0x88(%rbx), %rcx
    1499: 41 83 fb 01                  	cmpl	$0x1, %r11d
    149d: 75 05                        	jne	0x14a4 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x94>
    149f: 45 31 d2                     	xorl	%r10d, %r10d
    14a2: eb 70                        	jmp	0x1514 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x104>
    14a4: 45 89 dc                     	movl	%r11d, %r12d
    14a7: 41 83 e4 fe                  	andl	$-0x2, %r12d
    14ab: 45 31 d2                     	xorl	%r10d, %r10d
    14ae: 66 90                        	nop
    14b0: 47 8b 2c 91                  	movl	(%r9,%r10,4), %r13d
    14b4: f2 42 0f 10 04 d2            	movsd	(%rdx,%r10,8), %xmm0
    14ba: f2 43 0f 59 04 d0            	mulsd	(%r8,%r10,8), %xmm0
    14c0: f2 43 0f 11 04 ee            	movsd	%xmm0, (%r14,%r13,8)
    14c6: f2 42 0f 10 04 d2            	movsd	(%rdx,%r10,8), %xmm0
    14cc: f2 42 0f 59 04 d1            	mulsd	(%rcx,%r10,8), %xmm0
    14d2: f2 43 0f 11 04 ef            	movsd	%xmm0, (%r15,%r13,8)
    14d8: 47 8b 6c 91 04               	movl	0x4(%r9,%r10,4), %r13d
    14dd: f2 42 0f 10 44 d2 08         	movsd	0x8(%rdx,%r10,8), %xmm0
    14e4: f2 43 0f 59 44 d0 08         	mulsd	0x8(%r8,%r10,8), %xmm0
    14eb: f2 43 0f 11 04 ee            	movsd	%xmm0, (%r14,%r13,8)
    14f1: f2 42 0f 10 44 d2 08         	movsd	0x8(%rdx,%r10,8), %xmm0
    14f8: f2 42 0f 59 44 d1 08         	mulsd	0x8(%rcx,%r10,8), %xmm0
    14ff: f2 43 0f 11 04 ef            	movsd	%xmm0, (%r15,%r13,8)
    1505: 49 83 c2 02                  	addq	$0x2, %r10
    1509: 4d 39 d4                     	cmpq	%r10, %r12
    150c: 75 a2                        	jne	0x14b0 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0xa0>
    150e: 41 f6 c3 01                  	testb	$0x1, %r11b
    1512: 74 28                        	je	0x153c <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x12c>
    1514: 47 8b 0c 91                  	movl	(%r9,%r10,4), %r9d
    1518: f2 42 0f 10 04 d2            	movsd	(%rdx,%r10,8), %xmm0
    151e: f2 43 0f 59 04 d0            	mulsd	(%r8,%r10,8), %xmm0
    1524: f2 43 0f 11 04 ce            	movsd	%xmm0, (%r14,%r9,8)
    152a: f2 42 0f 10 04 d2            	movsd	(%rdx,%r10,8), %xmm0
    1530: f2 42 0f 59 04 d1            	mulsd	(%rcx,%r10,8), %xmm0
    1536: f2 43 0f 11 04 cf            	movsd	%xmm0, (%r15,%r9,8)
    153c: 83 fd 02                     	cmpl	$0x2, %ebp
    153f: 72 46                        	jb	0x1587 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x177>
    1541: 4c 8b b4 24 a8 00 00 00      	movq	0xa8(%rsp), %r14
    1549: 41 bf 02 00 00 00            	movl	$0x2, %r15d
    154f: 90                           	nop
    1550: 44 89 f8                     	movl	%r15d, %eax
    1553: d1 e8                        	shrl	%eax
    1555: 48 8b 4b 28                  	movq	0x28(%rbx), %rcx
    1559: 48 8b 53 48                  	movq	0x48(%rbx), %rdx
    155d: 4c 8d 04 c1                  	leaq	(%rcx,%rax,8), %r8
    1561: 49 83 c0 f8                  	addq	$-0x8, %r8
    1565: 4c 8d 4c c2 f8               	leaq	-0x8(%rdx,%rax,8), %r9
    156a: 48 8b 0f                     	movq	(%rdi), %rcx
    156d: 48 8b 16                     	movq	(%rsi), %rdx
    1570: 48 89 6c 24 28               	movq	%rbp, 0x28(%rsp)
    1575: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
    157a: 41 ff d6                     	callq	*%r14
    157d: 45 01 ff                     	addl	%r15d, %r15d
    1580: 41 39 ef                     	cmpl	%ebp, %r15d
    1583: 76 cb                        	jbe	0x1550 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x140>
    1585: 8b 03                        	movl	(%rbx), %eax
    1587: c1 e8 02                     	shrl	$0x2, %eax
    158a: 89 e9                        	movl	%ebp, %ecx
    158c: 29 c1                        	subl	%eax, %ecx
    158e: 0f 84 e5 00 00 00            	je	0x1679 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x269>
    1594: 48 8b 17                     	movq	(%rdi), %rdx
    1597: 4c 8b 83 a8 00 00 00         	movq	0xa8(%rbx), %r8
    159e: 4c 8b 8b c8 00 00 00         	movq	0xc8(%rbx), %r9
    15a5: 4c 8b 16                     	movq	(%rsi), %r10
    15a8: 4c 8b 5c 24 30               	movq	0x30(%rsp), %r11
    15ad: 4d 8b 1b                     	movq	(%r11), %r11
    15b0: 41 89 ce                     	movl	%ecx, %r14d
    15b3: 83 f9 10                     	cmpl	$0x10, %ecx
    15b6: 72 1f                        	jb	0x15d7 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1c7>
    15b8: 4d 8d 7e ff                  	leaq	-0x1(%r14), %r15
    15bc: 41 89 c4                     	movl	%eax, %r12d
    15bf: 45 01 fc                     	addl	%r15d, %r12d
    15c2: 41 0f 92 c4                  	setb	%r12b
    15c6: 49 c1 ef 20                  	shrq	$0x20, %r15
    15ca: 41 0f 95 c7                  	setne	%r15b
    15ce: 45 08 e7                     	orb	%r12b, %r15b
    15d1: 0f 84 78 02 00 00            	je	0x184f <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x43f>
    15d7: 45 31 ff                     	xorl	%r15d, %r15d
    15da: 4d 89 fc                     	movq	%r15, %r12
    15dd: 41 f6 c6 01                  	testb	$0x1, %r14b
    15e1: 74 2d                        	je	0x1610 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x200>
    15e3: 46 8d 24 38                  	leal	(%rax,%r15), %r12d
    15e7: f2 42 0f 10 04 e2            	movsd	(%rdx,%r12,8), %xmm0
    15ed: f2 43 0f 59 04 f8            	mulsd	(%r8,%r15,8), %xmm0
    15f3: f2 43 0f 10 0c e2            	movsd	(%r10,%r12,8), %xmm1
    15f9: f2 43 0f 59 0c f9            	mulsd	(%r9,%r15,8), %xmm1
    15ff: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    1603: f2 43 0f 11 04 fb            	movsd	%xmm0, (%r11,%r15,8)
    1609: 4d 89 fc                     	movq	%r15, %r12
    160c: 49 83 cc 01                  	orq	$0x1, %r12
    1610: 4d 8d 6e ff                  	leaq	-0x1(%r14), %r13
    1614: 4d 39 ef                     	cmpq	%r13, %r15
    1617: 74 60                        	je	0x1679 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x269>
    1619: 0f 1f 80 00 00 00 00         	nopl	(%rax)
    1620: 46 8d 3c 20                  	leal	(%rax,%r12), %r15d
    1624: f2 42 0f 10 04 fa            	movsd	(%rdx,%r15,8), %xmm0
    162a: f2 43 0f 59 04 e0            	mulsd	(%r8,%r12,8), %xmm0
    1630: f2 43 0f 10 0c fa            	movsd	(%r10,%r15,8), %xmm1
    1636: f2 43 0f 59 0c e1            	mulsd	(%r9,%r12,8), %xmm1
    163c: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    1640: f2 43 0f 11 04 e3            	movsd	%xmm0, (%r11,%r12,8)
    1646: 46 8d 7c 20 01               	leal	0x1(%rax,%r12), %r15d
    164b: f2 42 0f 10 04 fa            	movsd	(%rdx,%r15,8), %xmm0
    1651: f2 43 0f 59 44 e0 08         	mulsd	0x8(%r8,%r12,8), %xmm0
    1658: f2 43 0f 10 0c fa            	movsd	(%r10,%r15,8), %xmm1
    165e: f2 43 0f 59 4c e1 08         	mulsd	0x8(%r9,%r12,8), %xmm1
    1665: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    1669: f2 43 0f 11 44 e3 08         	movsd	%xmm0, 0x8(%r11,%r12,8)
    1670: 49 83 c4 02                  	addq	$0x2, %r12
    1674: 4d 39 e6                     	cmpq	%r12, %r14
    1677: 75 a7                        	jne	0x1620 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x210>
    1679: 39 e9                        	cmpl	%ebp, %ecx
    167b: 0f 83 bd 01 00 00            	jae	0x183e <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x42e>
    1681: 48 8b 07                     	movq	(%rdi), %rax
    1684: 48 8b 93 a8 00 00 00         	movq	0xa8(%rbx), %rdx
    168b: 4c 8b 83 c8 00 00 00         	movq	0xc8(%rbx), %r8
    1692: 4c 8b 0e                     	movq	(%rsi), %r9
    1695: 4c 8b 54 24 30               	movq	0x30(%rsp), %r10
    169a: 4d 8b 12                     	movq	(%r10), %r10
    169d: 89 c9                        	movl	%ecx, %ecx
    169f: 48 89 ee                     	movq	%rbp, %rsi
    16a2: 48 29 ce                     	subq	%rcx, %rsi
    16a5: 49 89 cb                     	movq	%rcx, %r11
    16a8: 48 83 fe 0a                  	cmpq	$0xa, %rsi
    16ac: 0f 82 e9 00 00 00            	jb	0x179b <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x38b>
    16b2: 4d 8d 1c ca                  	leaq	(%r10,%rcx,8), %r11
    16b6: 4c 89 df                     	movq	%r11, %rdi
    16b9: 48 29 c7                     	subq	%rax, %rdi
    16bc: 48 83 ff 20                  	cmpq	$0x20, %rdi
    16c0: 0f 92 c3                     	setb	%bl
    16c3: 4c 89 d7                     	movq	%r10, %rdi
    16c6: 48 29 d7                     	subq	%rdx, %rdi
    16c9: 48 83 ff 20                  	cmpq	$0x20, %rdi
    16cd: 40 0f 92 c7                  	setb	%dil
    16d1: 40 08 df                     	orb	%bl, %dil
    16d4: 4d 29 cb                     	subq	%r9, %r11
    16d7: 49 83 fb 20                  	cmpq	$0x20, %r11
    16db: 41 0f 92 c3                  	setb	%r11b
    16df: 4c 89 d3                     	movq	%r10, %rbx
    16e2: 4c 29 c3                     	subq	%r8, %rbx
    16e5: 48 83 fb 20                  	cmpq	$0x20, %rbx
    16e9: 0f 92 c3                     	setb	%bl
    16ec: 44 08 db                     	orb	%r11b, %bl
    16ef: 40 08 fb                     	orb	%dil, %bl
    16f2: 49 89 cb                     	movq	%rcx, %r11
    16f5: 0f 85 a0 00 00 00            	jne	0x179b <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x38b>
    16fb: 4c 8d 3c cd 00 00 00 00      	leaq	(,%rcx,8), %r15
    1703: 48 89 f7                     	movq	%rsi, %rdi
    1706: 48 83 e7 fc                  	andq	$-0x4, %rdi
    170a: 4c 8d 1c 0f                  	leaq	(%rdi,%rcx), %r11
    170e: 4b 8d 1c 3a                  	leaq	(%r10,%r15), %rbx
    1712: 48 83 c3 10                  	addq	$0x10, %rbx
    1716: 4f 8d 34 38                  	leaq	(%r8,%r15), %r14
    171a: 49 83 c6 10                  	addq	$0x10, %r14
    171e: 49 01 d7                     	addq	%rdx, %r15
    1721: 49 83 c7 10                  	addq	$0x10, %r15
    1725: 45 31 e4                     	xorl	%r12d, %r12d
    1728: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
    1730: 66 42 0f 10 04 e0            	movupd	(%rax,%r12,8), %xmm0
    1736: 66 42 0f 10 4c e0 10         	movupd	0x10(%rax,%r12,8), %xmm1
    173d: 66 43 0f 10 54 e7 f0         	movupd	-0x10(%r15,%r12,8), %xmm2
    1744: 66 0f 59 d0                  	mulpd	%xmm0, %xmm2
    1748: 66 43 0f 10 04 e7            	movupd	(%r15,%r12,8), %xmm0
    174e: 66 0f 59 c1                  	mulpd	%xmm1, %xmm0
    1752: 66 43 0f 10 0c e1            	movupd	(%r9,%r12,8), %xmm1
    1758: 66 43 0f 10 5c e1 10         	movupd	0x10(%r9,%r12,8), %xmm3
    175f: 66 43 0f 10 64 e6 f0         	movupd	-0x10(%r14,%r12,8), %xmm4
    1766: 66 0f 59 e1                  	mulpd	%xmm1, %xmm4
    176a: 66 0f 5c d4                  	subpd	%xmm4, %xmm2
    176e: 66 43 0f 10 0c e6            	movupd	(%r14,%r12,8), %xmm1
    1774: 66 0f 59 cb                  	mulpd	%xmm3, %xmm1
    1778: 66 0f 5c c1                  	subpd	%xmm1, %xmm0
    177c: 66 42 0f 11 54 e3 f0         	movupd	%xmm2, -0x10(%rbx,%r12,8)
    1783: 66 42 0f 11 04 e3            	movupd	%xmm0, (%rbx,%r12,8)
    1789: 49 83 c4 04                  	addq	$0x4, %r12
    178d: 4c 39 e7                     	cmpq	%r12, %rdi
    1790: 75 9e                        	jne	0x1730 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x320>
    1792: 48 39 fe                     	cmpq	%rdi, %rsi
    1795: 0f 84 a3 00 00 00            	je	0x183e <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x42e>
    179b: 89 ef                        	movl	%ebp, %edi
    179d: 44 29 df                     	subl	%r11d, %edi
    17a0: 4c 89 de                     	movq	%r11, %rsi
    17a3: 40 f6 c7 01                  	testb	$0x1, %dil
    17a7: 74 2b                        	je	0x17d4 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x3c4>
    17a9: 4c 89 de                     	movq	%r11, %rsi
    17ac: 48 29 ce                     	subq	%rcx, %rsi
    17af: f2 0f 10 04 f0               	movsd	(%rax,%rsi,8), %xmm0
    17b4: f2 42 0f 59 04 da            	mulsd	(%rdx,%r11,8), %xmm0
    17ba: f2 41 0f 10 0c f1            	movsd	(%r9,%rsi,8), %xmm1
    17c0: f2 43 0f 59 0c d8            	mulsd	(%r8,%r11,8), %xmm1
    17c6: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    17ca: f2 43 0f 11 04 da            	movsd	%xmm0, (%r10,%r11,8)
    17d0: 49 8d 73 01                  	leaq	0x1(%r11), %rsi
    17d4: 48 8d 7d ff                  	leaq	-0x1(%rbp), %rdi
    17d8: 49 39 fb                     	cmpq	%rdi, %r11
    17db: 74 61                        	je	0x183e <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x42e>
    17dd: 48 c1 e1 03                  	shlq	$0x3, %rcx
    17e1: 41 bb 08 00 00 00            	movl	$0x8, %r11d
    17e7: 49 29 cb                     	subq	%rcx, %r11
    17ea: 4c 01 d8                     	addq	%r11, %rax
    17ed: 4d 01 d9                     	addq	%r11, %r9
    17f0: f2 0f 10 44 f0 f8            	movsd	-0x8(%rax,%rsi,8), %xmm0
    17f6: f2 0f 59 04 f2               	mulsd	(%rdx,%rsi,8), %xmm0
    17fb: f2 41 0f 10 4c f1 f8         	movsd	-0x8(%r9,%rsi,8), %xmm1
    1802: f2 41 0f 59 0c f0            	mulsd	(%r8,%rsi,8), %xmm1
    1808: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    180c: f2 41 0f 11 04 f2            	movsd	%xmm0, (%r10,%rsi,8)
    1812: f2 0f 10 04 f0               	movsd	(%rax,%rsi,8), %xmm0
    1817: f2 0f 59 44 f2 08            	mulsd	0x8(%rdx,%rsi,8), %xmm0
    181d: f2 41 0f 10 0c f1            	movsd	(%r9,%rsi,8), %xmm1
    1823: f2 41 0f 59 4c f0 08         	mulsd	0x8(%r8,%rsi,8), %xmm1
    182a: f2 0f 5c c1                  	subsd	%xmm1, %xmm0
    182e: f2 41 0f 11 44 f2 08         	movsd	%xmm0, 0x8(%r10,%rsi,8)
    1835: 48 83 c6 02                  	addq	$0x2, %rsi
    1839: 48 39 f5                     	cmpq	%rsi, %rbp
    183c: 75 b2                        	jne	0x17f0 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x3e0>
    183e: 48 83 c4 38                  	addq	$0x38, %rsp
    1842: 5b                           	popq	%rbx
    1843: 5d                           	popq	%rbp
    1844: 5f                           	popq	%rdi
    1845: 5e                           	popq	%rsi
    1846: 41 5c                        	popq	%r12
    1848: 41 5d                        	popq	%r13
    184a: 41 5e                        	popq	%r14
    184c: 41 5f                        	popq	%r15
    184e: c3                           	retq
    184f: 4d 89 dc                     	movq	%r11, %r12
    1852: 4d 29 c4                     	subq	%r8, %r12
    1855: 45 31 ff                     	xorl	%r15d, %r15d
    1858: 49 83 fc 20                  	cmpq	$0x20, %r12
    185c: 0f 82 78 fd ff ff            	jb	0x15da <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    1862: 4d 89 dc                     	movq	%r11, %r12
    1865: 4d 29 cc                     	subq	%r9, %r12
    1868: 49 83 fc 20                  	cmpq	$0x20, %r12
    186c: 0f 82 68 fd ff ff            	jb	0x15da <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    1872: 4c 8d 24 c2                  	leaq	(%rdx,%rax,8), %r12
    1876: 4d 89 dd                     	movq	%r11, %r13
    1879: 4d 29 e5                     	subq	%r12, %r13
    187c: 49 83 fd 20                  	cmpq	$0x20, %r13
    1880: 0f 82 54 fd ff ff            	jb	0x15da <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    1886: 4d 8d 24 c2                  	leaq	(%r10,%rax,8), %r12
    188a: 4d 89 dd                     	movq	%r11, %r13
    188d: 4d 29 e5                     	subq	%r12, %r13
    1890: 49 83 fd 20                  	cmpq	$0x20, %r13
    1894: 0f 82 40 fd ff ff            	jb	0x15da <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    189a: 45 89 f7                     	movl	%r14d, %r15d
    189d: 41 83 e7 fc                  	andl	$-0x4, %r15d
    18a1: 45 31 e4                     	xorl	%r12d, %r12d
    18a4: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
    18b0: 46 8d 2c 20                  	leal	(%rax,%r12), %r13d
    18b4: 66 42 0f 10 04 ea            	movupd	(%rdx,%r13,8), %xmm0
    18ba: 66 42 0f 10 4c ea 10         	movupd	0x10(%rdx,%r13,8), %xmm1
    18c1: 66 43 0f 10 14 e0            	movupd	(%r8,%r12,8), %xmm2
    18c7: 66 0f 59 d0                  	mulpd	%xmm0, %xmm2
    18cb: 66 43 0f 10 44 e0 10         	movupd	0x10(%r8,%r12,8), %xmm0
    18d2: 66 0f 59 c1                  	mulpd	%xmm1, %xmm0
    18d6: 66 43 0f 10 0c ea            	movupd	(%r10,%r13,8), %xmm1
    18dc: 66 43 0f 10 5c ea 10         	movupd	0x10(%r10,%r13,8), %xmm3
    18e3: 66 43 0f 10 24 e1            	movupd	(%r9,%r12,8), %xmm4
    18e9: 66 0f 59 e1                  	mulpd	%xmm1, %xmm4
    18ed: 66 0f 5c d4                  	subpd	%xmm4, %xmm2
    18f1: 66 43 0f 10 4c e1 10         	movupd	0x10(%r9,%r12,8), %xmm1
    18f8: 66 0f 59 cb                  	mulpd	%xmm3, %xmm1
    18fc: 66 0f 5c c1                  	subpd	%xmm1, %xmm0
    1900: 66 43 0f 11 14 e3            	movupd	%xmm2, (%r11,%r12,8)
    1906: 66 43 0f 11 44 e3 10         	movupd	%xmm0, 0x10(%r11,%r12,8)
    190d: 49 83 c4 04                  	addq	$0x4, %r12
    1911: 4d 39 e7                     	cmpq	%r12, %r15
    1914: 75 9a                        	jne	0x18b0 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x4a0>
    1916: 45 39 f7                     	cmpl	%r14d, %r15d
    1919: 0f 85 bb fc ff ff            	jne	0x15da <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x1ca>
    191f: e9 55 fd ff ff               	jmp	0x1679 <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x269>
    1924: 48 89 c1                     	movq	%rax, %rcx
    1927: e8 00 00 00 00               	callq	0x192c <stx_vorbis::detail::inverse_mdct(stx_vorbis::detail::Transform const&, std::__1::span<double const, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, std::__1::span<double, 18446744073709551615ull>, void (*)(double*, double*, double const*, double const*, unsigned long long, unsigned long long))+0x51c>
    192c: cc                           	int3

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
