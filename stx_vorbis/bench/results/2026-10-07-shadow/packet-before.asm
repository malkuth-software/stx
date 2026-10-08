
.build/stx-perf/CMakeFiles/stx_vorbis.dir/src/packet.cpp.obj:	file format coff-x86-64

Disassembly of section .text:

0000000000000000 <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)>:
       0: 41 57                        	pushq	%r15
       2: 41 56                        	pushq	%r14
       4: 56                           	pushq	%rsi
       5: 57                           	pushq	%rdi
       6: 53                           	pushq	%rbx
       7: 48 83 ec 20                  	subq	$0x20, %rsp
       b: 48 89 ce                     	movq	%rcx, %rsi
       e: 44 8b 7a 04                  	movl	0x4(%rdx), %r15d
      12: 44 8b 72 0c                  	movl	0xc(%rdx), %r14d
      16: 44 89 f1                     	movl	%r14d, %ecx
      19: d1 e9                        	shrl	%ecx
      1b: 89 8e dc 13 00 00            	movl	%ecx, 0x13dc(%rsi)
      21: 41 3b 48 50                  	cmpl	0x50(%r8), %ecx
      25: 0f 87 d4 00 00 00            	ja	0xff <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0xff>
      2b: 44 89 cb                     	movl	%r9d, %ebx
      2e: 4c 89 c7                     	movq	%r8, %rdi
      31: 85 c9                        	testl	%ecx, %ecx
      33: 74 28                        	je	0x5d <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0x5d>
      35: 48 8b 07                     	movq	(%rdi), %rax
      38: 48 c1 e8 03                  	shrq	$0x3, %rax
      3c: 48 ba 00 00 00 00 ff ff ff 1f	movabsq	$0x1fffffff00000000, %rdx # imm = 0x1FFFFFFF00000000
      46: 48 85 d0                     	testq	%rdx, %rax
      49: 0f 84 a3 00 00 00            	je	0xf2 <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0xf2>
      4f: 31 d2                        	xorl	%edx, %edx
      51: 48 f7 f1                     	divq	%rcx
      54: 4c 39 f8                     	cmpq	%r15, %rax
      57: 0f 82 a2 00 00 00            	jb	0xff <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0xff>
      5d: 4c 0f af f9                  	imulq	%rcx, %r15
      61: 48 89 f1                     	movq	%rsi, %rcx
      64: 4c 89 fa                     	movq	%r15, %rdx
      67: e8 00 00 00 00               	callq	0x6c <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0x6c>
      6c: 48 8d 4e 20                  	leaq	0x20(%rsi), %rcx
      70: 4c 89 fa                     	movq	%r15, %rdx
      73: e8 00 00 00 00               	callq	0x78 <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0x78>
      78: 48 8d 4e 60                  	leaq	0x60(%rsi), %rcx
      7c: 4c 89 fa                     	movq	%r15, %rdx
      7f: e8 00 00 00 00               	callq	0x84 <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0x84>
      84: 48 8d 4e 40                  	leaq	0x40(%rsi), %rcx
      88: 4b 8d 14 3f                  	leaq	(%r15,%r15), %rdx
      8c: e8 00 00 00 00               	callq	0x91 <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0x91>
      91: 48 8d 8e 80 00 00 00         	leaq	0x80(%rsi), %rcx
      98: 4c 89 fa                     	movq	%r15, %rdx
      9b: e8 00 00 00 00               	callq	0xa0 <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0xa0>
      a0: 48 8d 8e e0 00 00 00         	leaq	0xe0(%rsi), %rcx
      a7: 4c 89 fa                     	movq	%r15, %rdx
      aa: e8 00 00 00 00               	callq	0xaf <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0xaf>
      af: 48 8d 8e a0 00 00 00         	leaq	0xa0(%rsi), %rcx
      b6: 4c 89 f2                     	movq	%r14, %rdx
      b9: e8 00 00 00 00               	callq	0xbe <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0xbe>
      be: 48 8d 8e c0 00 00 00         	leaq	0xc0(%rsi), %rcx
      c5: 4c 89 f2                     	movq	%r14, %rdx
      c8: e8 00 00 00 00               	callq	0xcd <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0xcd>
      cd: 48 8b 47 38                  	movq	0x38(%rdi), %rax
      d1: 48 89 86 f0 13 00 00         	movq	%rax, 0x13f0(%rsi)
      d8: 89 d9                        	movl	%ebx, %ecx
      da: e8 00 00 00 00               	callq	0xdf <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0xdf>
      df: 48 89 86 f8 13 00 00         	movq	%rax, 0x13f8(%rsi)
      e6: 48 83 c4 20                  	addq	$0x20, %rsp
      ea: 5b                           	popq	%rbx
      eb: 5f                           	popq	%rdi
      ec: 5e                           	popq	%rsi
      ed: 41 5e                        	popq	%r14
      ef: 41 5f                        	popq	%r15
      f1: c3                           	retq
      f2: 31 d2                        	xorl	%edx, %edx
      f4: f7 f1                        	divl	%ecx
      f6: 4c 39 f8                     	cmpq	%r15, %rax
      f9: 0f 83 5e ff ff ff            	jae	0x5d <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0x5d>
      ff: b9 10 00 00 00               	movl	$0x10, %ecx
     104: e8 00 00 00 00               	callq	0x109 <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0x109>
     109: c6 00 0d                     	movb	$0xd, (%rax)
     10c: 48 c7 40 08 00 00 00 00      	movq	$0x0, 0x8(%rax)
     114: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x11b <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0x11b>
     11b: 48 89 c1                     	movq	%rax, %rcx
     11e: 45 31 c0                     	xorl	%r8d, %r8d
     121: e8 00 00 00 00               	callq	0x126 <stx_vorbis::detail::prepare_workspace(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, stx_vorbis::Limits const&, stx_vorbis::Synthesis)+0x126>
     126: cc                           	int3
     127: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)

0000000000000130 <stx_vorbis::detail::reset_overlap(stx_vorbis::detail::Workspace&)>:
     130: 48 89 c8                     	movq	%rcx, %rax
     133: c7 81 d8 13 00 00 00 00 00 00	movl	$0x0, 0x13d8(%rcx)
     13d: c7 81 e0 13 00 00 00 00 00 00	movl	$0x0, 0x13e0(%rcx)
     147: 48 8b 49 60                  	movq	0x60(%rcx), %rcx
     14b: 4c 8b 40 68                  	movq	0x68(%rax), %r8
     14f: 49 29 c8                     	subq	%rcx, %r8
     152: 4d 85 c0                     	testq	%r8, %r8
     155: 7e 07                        	jle	0x15e <stx_vorbis::detail::reset_overlap(stx_vorbis::detail::Workspace&)+0x2e>
     157: 31 d2                        	xorl	%edx, %edx
     159: e9 00 00 00 00               	jmp	0x15e <stx_vorbis::detail::reset_overlap(stx_vorbis::detail::Workspace&)+0x2e>
     15e: c3                           	retq
     15f: 90                           	nop

0000000000000160 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)>:
     160: 41 57                        	pushq	%r15
     162: 41 56                        	pushq	%r14
     164: 41 55                        	pushq	%r13
     166: 41 54                        	pushq	%r12
     168: 56                           	pushq	%rsi
     169: 57                           	pushq	%rdi
     16a: 55                           	pushq	%rbp
     16b: 53                           	pushq	%rbx
     16c: 48 81 ec 38 02 00 00         	subq	$0x238, %rsp            # imm = 0x238
     173: 66 44 0f 29 bc 24 20 02 00 00	movapd	%xmm15, 0x220(%rsp)
     17d: 66 44 0f 29 b4 24 10 02 00 00	movapd	%xmm14, 0x210(%rsp)
     187: 66 44 0f 29 ac 24 00 02 00 00	movapd	%xmm13, 0x200(%rsp)
     191: 66 44 0f 29 a4 24 f0 01 00 00	movapd	%xmm12, 0x1f0(%rsp)
     19b: 66 44 0f 29 9c 24 e0 01 00 00	movapd	%xmm11, 0x1e0(%rsp)
     1a5: 66 44 0f 29 94 24 d0 01 00 00	movapd	%xmm10, 0x1d0(%rsp)
     1af: 66 44 0f 29 8c 24 c0 01 00 00	movapd	%xmm9, 0x1c0(%rsp)
     1b9: 66 44 0f 29 84 24 b0 01 00 00	movapd	%xmm8, 0x1b0(%rsp)
     1c3: 66 0f 29 bc 24 a0 01 00 00   	movapd	%xmm7, 0x1a0(%rsp)
     1cc: 66 0f 29 b4 24 90 01 00 00   	movapd	%xmm6, 0x190(%rsp)
     1d5: 49 89 d4                     	movq	%rdx, %r12
     1d8: 48 89 ce                     	movq	%rcx, %rsi
     1db: 48 c7 81 e8 13 00 00 00 00 00 00     	movq	$0x0, 0x13e8(%rcx)
     1e6: c7 81 e0 13 00 00 00 00 00 00	movl	$0x0, 0x13e0(%rcx)
     1f0: 66 41 0f 10 00               	movupd	(%r8), %xmm0
     1f5: 66 0f 29 84 24 90 00 00 00   	movapd	%xmm0, 0x90(%rsp)
     1fe: 48 c7 84 24 a0 00 00 00 00 00 00 00  	movq	$0x0, 0xa0(%rsp)
     20a: c7 44 24 70 00 00 00 00      	movl	$0x0, 0x70(%rsp)
     212: 48 8d 8c 24 90 00 00 00      	leaq	0x90(%rsp), %rcx
     21a: 4c 8d 44 24 70               	leaq	0x70(%rsp), %r8
     21f: ba 01 00 00 00               	movl	$0x1, %edx
     224: e8 00 00 00 00               	callq	0x229 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xc9>
     229: 84 c0                        	testb	%al, %al
     22b: 0f 84 cb 1a 00 00            	je	0x1cfc <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b9c>
     231: 83 7c 24 70 00               	cmpl	$0x0, 0x70(%rsp)
     236: 0f 85 c1 24 00 00            	jne	0x26fd <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x259d>
     23c: 49 8b 84 24 98 00 00 00      	movq	0x98(%r12), %rax
     244: 49 2b 84 24 90 00 00 00      	subq	0x90(%r12), %rax
     24c: 48 c1 e8 03                  	shrq	$0x3, %rax
     250: ff c8                        	decl	%eax
     252: 0f bd d0                     	bsrl	%eax, %edx
     255: ff c2                        	incl	%edx
     257: 85 c0                        	testl	%eax, %eax
     259: 0f 44 d0                     	cmovel	%eax, %edx
     25c: c7 44 24 70 00 00 00 00      	movl	$0x0, 0x70(%rsp)
     264: 48 8d 8c 24 90 00 00 00      	leaq	0x90(%rsp), %rcx
     26c: 4c 8d 44 24 70               	leaq	0x70(%rsp), %r8
     271: e8 00 00 00 00               	callq	0x276 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x116>
     276: 84 c0                        	testb	%al, %al
     278: 0f 84 7e 1a 00 00            	je	0x1cfc <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b9c>
     27e: 8b 4c 24 70                  	movl	0x70(%rsp), %ecx
     282: 49 8b 94 24 90 00 00 00      	movq	0x90(%r12), %rdx
     28a: 49 8b 84 24 98 00 00 00      	movq	0x98(%r12), %rax
     292: 48 29 d0                     	subq	%rdx, %rax
     295: 48 c1 f8 03                  	sarq	$0x3, %rax
     299: 48 39 c8                     	cmpq	%rcx, %rax
     29c: 0f 86 71 1a 00 00            	jbe	0x1d13 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1bb3>
     2a2: 48 89 8c 24 18 01 00 00      	movq	%rcx, 0x118(%rsp)
     2aa: 48 89 94 24 10 01 00 00      	movq	%rdx, 0x110(%rsp)
     2b2: 0f b6 14 ca                  	movzbl	(%rdx,%rcx,8), %edx
     2b6: 41 8b 7c 94 08               	movl	0x8(%r12,%rdx,4), %edi
     2bb: 45 8b 6c 24 04               	movl	0x4(%r12), %r13d
     2c0: 49 89 f8                     	movq	%rdi, %r8
     2c3: 4d 0f af c5                  	imulq	%r13, %r8
     2c7: 49 c1 e0 02                  	shlq	$0x2, %r8
     2cb: 48 8b 86 e8 13 00 00         	movq	0x13e8(%rsi), %rax
     2d2: 48 8b 8e f0 13 00 00         	movq	0x13f0(%rsi), %rcx
     2d9: 49 89 c9                     	movq	%rcx, %r9
     2dc: 49 29 c1                     	subq	%rax, %r9
     2df: 4d 39 c8                     	cmpq	%r9, %r8
     2e2: 0f 87 3c 24 00 00            	ja	0x2724 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25c4>
     2e8: 4c 01 c0                     	addq	%r8, %rax
     2eb: 48 89 86 e8 13 00 00         	movq	%rax, 0x13e8(%rsi)
     2f2: 84 d2                        	testb	%dl, %dl
     2f4: 48 89 74 24 60               	movq	%rsi, 0x60(%rsp)
     2f9: 0f 84 82 00 00 00            	je	0x381 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x221>
     2ff: c7 44 24 70 00 00 00 00      	movl	$0x0, 0x70(%rsp)
     307: 48 8d 8c 24 90 00 00 00      	leaq	0x90(%rsp), %rcx
     30f: 4c 8d 44 24 70               	leaq	0x70(%rsp), %r8
     314: ba 01 00 00 00               	movl	$0x1, %edx
     319: e8 00 00 00 00               	callq	0x31e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1be>
     31e: 84 c0                        	testb	%al, %al
     320: 0f 84 d6 19 00 00            	je	0x1cfc <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b9c>
     326: 8b 74 24 70                  	movl	0x70(%rsp), %esi
     32a: c7 44 24 70 00 00 00 00      	movl	$0x0, 0x70(%rsp)
     332: 48 8d 8c 24 90 00 00 00      	leaq	0x90(%rsp), %rcx
     33a: 4c 8d 44 24 70               	leaq	0x70(%rsp), %r8
     33f: ba 01 00 00 00               	movl	$0x1, %edx
     344: e8 00 00 00 00               	callq	0x349 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1e9>
     349: 84 c0                        	testb	%al, %al
     34b: 0f 84 ab 19 00 00            	je	0x1cfc <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b9c>
     351: 85 f6                        	testl	%esi, %esi
     353: 0f 95 c0                     	setne	%al
     356: 89 84 24 e8 00 00 00         	movl	%eax, 0xe8(%rsp)
     35d: 83 7c 24 70 00               	cmpl	$0x0, 0x70(%rsp)
     362: 0f 95 c0                     	setne	%al
     365: 89 84 24 ec 00 00 00         	movl	%eax, 0xec(%rsp)
     36c: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
     371: 48 8b 86 e8 13 00 00         	movq	0x13e8(%rsi), %rax
     378: 48 8b 8e f0 13 00 00         	movq	0x13f0(%rsi), %rcx
     37f: eb 16                        	jmp	0x397 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x237>
     381: c7 84 24 ec 00 00 00 00 00 00 00     	movl	$0x0, 0xec(%rsp)
     38c: c7 84 24 e8 00 00 00 00 00 00 00     	movl	$0x0, 0xe8(%rsp)
     397: 48 89 bc 24 48 01 00 00      	movq	%rdi, 0x148(%rsp)
     39f: 89 fd                        	movl	%edi, %ebp
     3a1: d1 ed                        	shrl	%ebp
     3a3: 48 8b 94 24 18 01 00 00      	movq	0x118(%rsp), %rdx
     3ab: 4c 8b 84 24 10 01 00 00      	movq	0x110(%rsp), %r8
     3b3: 41 8b 54 d0 04               	movl	0x4(%r8,%rdx,8), %edx
     3b8: 4d 8b 44 24 70               	movq	0x70(%r12), %r8
     3bd: 4c 69 f2 a0 04 00 00         	imulq	$0x4a0, %rdx, %r14      # imm = 0x4A0
     3c4: 4b 8b bc 30 80 04 00 00      	movq	0x480(%r8,%r14), %rdi
     3cc: 4c 89 84 24 b0 00 00 00      	movq	%r8, 0xb0(%rsp)
     3d4: 4b 8b 9c 30 88 04 00 00      	movq	0x488(%r8,%r14), %rbx
     3dc: 48 89 da                     	movq	%rbx, %rdx
     3df: 48 29 fa                     	subq	%rdi, %rdx
     3e2: 48 c1 fa 03                  	sarq	$0x3, %rdx
     3e6: 48 0f af d5                  	imulq	%rbp, %rdx
     3ea: 48 29 c1                     	subq	%rax, %rcx
     3ed: 48 39 ca                     	cmpq	%rcx, %rdx
     3f0: 0f 87 2e 23 00 00            	ja	0x2724 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25c4>
     3f6: 48 01 c2                     	addq	%rax, %rdx
     3f9: 48 89 96 e8 13 00 00         	movq	%rdx, 0x13e8(%rsi)
     400: 48 8b 0e                     	movq	(%rsi), %rcx
     403: 4c 8b 46 08                  	movq	0x8(%rsi), %r8
     407: 49 29 c8                     	subq	%rcx, %r8
     40a: 4d 85 c0                     	testq	%r8, %r8
     40d: 7e 07                        	jle	0x416 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2b6>
     40f: 31 d2                        	xorl	%edx, %edx
     411: e8 00 00 00 00               	callq	0x416 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2b6>
     416: 4c 01 b4 24 b0 00 00 00      	addq	%r14, 0xb0(%rsp)
     41e: 4d 85 ed                     	testq	%r13, %r13
     421: 4c 89 a4 24 d8 00 00 00      	movq	%r12, 0xd8(%rsp)
     429: 4c 89 6c 24 30               	movq	%r13, 0x30(%rsp)
     42e: 48 89 6c 24 40               	movq	%rbp, 0x40(%rsp)
     433: 0f 84 bb 0f 00 00            	je	0x13f4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1294>
     439: 48 8d 86 e4 0a 00 00         	leaq	0xae4(%rsi), %rax
     440: 48 89 84 24 80 00 00 00      	movq	%rax, 0x80(%rsp)
     448: 48 8d 86 e0 0b 00 00         	leaq	0xbe0(%rsi), %rax
     44f: 48 89 84 24 e0 00 00 00      	movq	%rax, 0xe0(%rsp)
     457: 4c 8d bc 24 90 00 00 00      	leaq	0x90(%rsp), %r15
     45f: 66 0f 57 f6                  	xorpd	%xmm6, %xmm6
     463: 66 44 0f 28 05 10 00 00 00   	movapd	0x10(%rip), %xmm8       # 0x47c <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x31c>
     46c: 66 44 0f 28 0d 20 00 00 00   	movapd	0x20(%rip), %xmm9       # 0x495 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x335>
     475: f2 44 0f 10 15 38 00 00 00   	movsd	0x38(%rip), %xmm10      # 0x4b6 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x356>
     47e: 66 45 0f 57 db               	xorpd	%xmm11, %xmm11
     483: f2 44 0f 10 25 40 00 00 00   	movsd	0x40(%rip), %xmm12      # 0x4cc <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x36c>
     48c: f2 44 0f 10 2d 30 00 00 00   	movsd	0x30(%rip), %xmm13      # 0x4c5 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x365>
     495: 48 c7 44 24 78 00 00 00 00   	movq	$0x0, 0x78(%rsp)
     49e: 31 c0                        	xorl	%eax, %eax
     4a0: 48 8b 8c 24 10 01 00 00      	movq	0x110(%rsp), %rcx
     4a8: eb 47                        	jmp	0x4f1 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x391>
     4aa: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     4b0: 48 83 7c 24 68 00            	cmpq	$0x0, 0x68(%rsp)
     4b6: 0f 95 c0                     	setne	%al
     4b9: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
     4be: 48 8b 8c 24 b8 00 00 00      	movq	0xb8(%rsp), %rcx
     4c6: 88 84 0e 00 01 00 00         	movb	%al, 0x100(%rsi,%rcx)
     4cd: 88 84 0e ff 01 00 00         	movb	%al, 0x1ff(%rsi,%rcx)
     4d4: 48 89 c8                     	movq	%rcx, %rax
     4d7: 48 ff c0                     	incq	%rax
     4da: 48 83 44 24 78 08            	addq	$0x8, 0x78(%rsp)
     4e0: 4c 39 e8                     	cmpq	%r13, %rax
     4e3: 48 8b 8c 24 10 01 00 00      	movq	0x110(%rsp), %rcx
     4eb: 0f 84 d2 0e 00 00            	je	0x13c3 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1263>
     4f1: 48 8b 94 24 b0 00 00 00      	movq	0xb0(%rsp), %rdx
     4f9: 48 89 84 24 b8 00 00 00      	movq	%rax, 0xb8(%rsp)
     501: 8b 44 82 04                  	movl	0x4(%rdx,%rax,4), %eax
     505: 8b 84 82 00 04 00 00         	movl	0x400(%rdx,%rax,4), %eax
     50c: 49 8b 54 24 30               	movq	0x30(%r12), %rdx
     511: 4c 69 c0 88 13 00 00         	imulq	$0x1388, %rax, %r8      # imm = 0x1388
     518: 4a 8d 1c 02                  	leaq	(%rdx,%r8), %rbx
     51c: 48 8b 46 20                  	movq	0x20(%rsi), %rax
     520: 48 89 84 24 c0 00 00 00      	movq	%rax, 0xc0(%rsp)
     528: 8b 86 dc 13 00 00            	movl	0x13dc(%rsi), %eax
     52e: 48 89 44 24 58               	movq	%rax, 0x58(%rsp)
     533: 48 89 54 24 68               	movq	%rdx, 0x68(%rsp)
     538: 4c 89 44 24 48               	movq	%r8, 0x48(%rsp)
     53d: 42 83 3c 02 01               	cmpl	$0x1, (%rdx,%r8)
     542: 48 89 5c 24 38               	movq	%rbx, 0x38(%rsp)
     547: 0f 85 83 02 00 00            	jne	0x7d0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x670>
     54d: c7 84 24 f4 00 00 00 00 00 00 00     	movl	$0x0, 0xf4(%rsp)
     558: 4c 89 f9                     	movq	%r15, %rcx
     55b: ba 01 00 00 00               	movl	$0x1, %edx
     560: 4c 8d 84 24 f4 00 00 00      	leaq	0xf4(%rsp), %r8
     568: e8 00 00 00 00               	callq	0x56d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x40d>
     56d: 84 c0                        	testb	%al, %al
     56f: 0f 84 ef 0d 00 00            	je	0x1364 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1204>
     575: 83 bc 24 f4 00 00 00 00      	cmpl	$0x0, 0xf4(%rsp)
     57d: 0f 84 39 0e 00 00            	je	0x13bc <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x125c>
     583: 8b 83 44 03 00 00            	movl	0x344(%rbx), %eax
     589: ff c8                        	decl	%eax
     58b: 48 8d 0d 70 00 00 00         	leaq	0x70(%rip), %rcx        # 0x602 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x4a2>
     592: 8b 04 81                     	movl	(%rcx,%rax,4), %eax
     595: 48 89 84 24 a8 00 00 00      	movq	%rax, 0xa8(%rsp)
     59d: ff c8                        	decl	%eax
     59f: 44 0f bd f0                  	bsrl	%eax, %r14d
     5a3: 41 ff c6                     	incl	%r14d
     5a6: 85 c0                        	testl	%eax, %eax
     5a8: 44 0f 44 f0                  	cmovel	%eax, %r14d
     5ac: c7 84 24 f8 00 00 00 00 00 00 00     	movl	$0x0, 0xf8(%rsp)
     5b7: 4c 89 f9                     	movq	%r15, %rcx
     5ba: 44 89 f2                     	movl	%r14d, %edx
     5bd: 4c 8d 84 24 f8 00 00 00      	leaq	0xf8(%rsp), %r8
     5c5: e8 00 00 00 00               	callq	0x5ca <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x46a>
     5ca: 84 c0                        	testb	%al, %al
     5cc: 0f 84 92 0d 00 00            	je	0x1364 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1204>
     5d2: 8b 84 24 f8 00 00 00         	movl	0xf8(%rsp), %eax
     5d9: 89 86 fc 06 00 00            	movl	%eax, 0x6fc(%rsi)
     5df: c7 84 24 fc 00 00 00 00 00 00 00     	movl	$0x0, 0xfc(%rsp)
     5ea: 4c 89 f9                     	movq	%r15, %rcx
     5ed: 44 89 f2                     	movl	%r14d, %edx
     5f0: 4c 8d 84 24 fc 00 00 00      	leaq	0xfc(%rsp), %r8
     5f8: e8 00 00 00 00               	callq	0x5fd <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x49d>
     5fd: 84 c0                        	testb	%al, %al
     5ff: 0f 84 5f 0d 00 00            	je	0x1364 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1204>
     605: 8b 84 24 fc 00 00 00         	movl	0xfc(%rsp), %eax
     60c: 89 86 00 07 00 00            	movl	%eax, 0x700(%rsi)
     612: 48 8b 8c 24 a8 00 00 00      	movq	0xa8(%rsp), %rcx
     61a: 39 8e fc 06 00 00            	cmpl	%ecx, 0x6fc(%rsi)
     620: 0f 8d 4e 0d 00 00            	jge	0x1374 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1214>
     626: 39 c8                        	cmpl	%ecx, %eax
     628: 0f 8d 5b 0d 00 00            	jge	0x1389 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1229>
     62e: 83 7b 04 00                  	cmpl	$0x0, 0x4(%rbx)
     632: 0f 84 28 05 00 00            	je	0xb60 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xa00>
     638: 48 8d 83 84 00 00 00         	leaq	0x84(%rbx), %rax
     63f: 48 89 84 24 88 00 00 00      	movq	%rax, 0x88(%rsp)
     647: 41 bd 02 00 00 00            	movl	$0x2, %r13d
     64d: 31 ed                        	xorl	%ebp, %ebp
     64f: eb 37                        	jmp	0x688 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x528>
     651: 66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00 	nopw	%cs:(%rax,%rax)
     660: 45 01 fd                     	addl	%r15d, %r13d
     663: 4c 8d bc 24 90 00 00 00      	leaq	0x90(%rsp), %r15
     66b: 48 ff c5                     	incq	%rbp
     66e: 48 8b 44 24 68               	movq	0x68(%rsp), %rax
     673: 48 8b 4c 24 48               	movq	0x48(%rsp), %rcx
     678: 48 8d 1c 08                  	leaq	(%rax,%rcx), %rbx
     67c: 8b 43 04                     	movl	0x4(%rbx), %eax
     67f: 48 39 c5                     	cmpq	%rax, %rbp
     682: 0f 83 d8 04 00 00            	jae	0xb60 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xa00>
     688: 8b 44 ab 08                  	movl	0x8(%rbx,%rbp,4), %eax
     68c: 48 6b c0 2c                  	imulq	$0x2c, %rax, %rax
     690: 48 8b 8c 24 88 00 00 00      	movq	0x88(%rsp), %rcx
     698: 48 8d 1c 01                  	leaq	(%rcx,%rax), %rbx
     69c: 83 7c 01 04 00               	cmpl	$0x0, 0x4(%rcx,%rax)
     6a1: 74 6d                        	je	0x710 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x5b0>
     6a3: 48 8b 86 e8 13 00 00         	movq	0x13e8(%rsi), %rax
     6aa: 48 39 86 f0 13 00 00         	cmpq	%rax, 0x13f0(%rsi)
     6b1: 0f 84 9d 0c 00 00            	je	0x1354 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11f4>
     6b7: 8b 4b 08                     	movl	0x8(%rbx), %ecx
     6ba: 48 69 c9 50 20 00 00         	imulq	$0x2050, %rcx, %rcx     # imm = 0x2050
     6c1: 49 03 4c 24 10               	addq	0x10(%r12), %rcx
     6c6: 48 ff c0                     	incq	%rax
     6c9: 48 89 86 e8 13 00 00         	movq	%rax, 0x13e8(%rsi)
     6d0: c7 84 24 00 01 00 00 00 00 00 00     	movl	$0x0, 0x100(%rsp)
     6db: 4c 89 fa                     	movq	%r15, %rdx
     6de: 4c 8d 84 24 00 01 00 00      	leaq	0x100(%rsp), %r8
     6e6: e8 00 00 00 00               	callq	0x6eb <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x58b>
     6eb: 84 c0                        	testb	%al, %al
     6ed: 0f 85 1a 0c 00 00            	jne	0x130d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11ad>
     6f3: 8b bc 24 00 01 00 00         	movl	0x100(%rsp), %edi
     6fa: 8b 4b 04                     	movl	0x4(%rbx), %ecx
     6fd: 41 be ff ff ff ff            	movl	$0xffffffff, %r14d      # imm = 0xFFFFFFFF
     703: 41 d3 e6                     	shll	%cl, %r14d
     706: 83 3b 00                     	cmpl	$0x0, (%rbx)
     709: 75 1b                        	jne	0x726 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x5c6>
     70b: e9 5b ff ff ff               	jmp	0x66b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x50b>
     710: 31 c9                        	xorl	%ecx, %ecx
     712: 31 ff                        	xorl	%edi, %edi
     714: 41 be ff ff ff ff            	movl	$0xffffffff, %r14d      # imm = 0xFFFFFFFF
     71a: 41 d3 e6                     	shll	%cl, %r14d
     71d: 83 3b 00                     	cmpl	$0x0, (%rbx)
     720: 0f 84 45 ff ff ff            	je	0x66b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x50b>
     726: 41 f7 d6                     	notl	%r14d
     729: 45 31 ff                     	xorl	%r15d, %r15d
     72c: eb 22                        	jmp	0x750 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x5f0>
     72e: 66 90                        	nop
     730: 8b 84 24 04 01 00 00         	movl	0x104(%rsp), %eax
     737: 43 8d 0c 2f                  	leal	(%r15,%r13), %ecx
     73b: 89 c9                        	movl	%ecx, %ecx
     73d: 89 84 8e fc 06 00 00         	movl	%eax, 0x6fc(%rsi,%rcx,4)
     744: 41 ff c7                     	incl	%r15d
     747: 44 3b 3b                     	cmpl	(%rbx), %r15d
     74a: 0f 83 10 ff ff ff            	jae	0x660 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x500>
     750: 89 f8                        	movl	%edi, %eax
     752: 0f b6 4b 04                  	movzbl	0x4(%rbx), %ecx
     756: d3 ef                        	shrl	%cl, %edi
     758: 44 21 f0                     	andl	%r14d, %eax
     75b: 48 63 4c 83 0c               	movslq	0xc(%rbx,%rax,4), %rcx
     760: b8 00 00 00 00               	movl	$0x0, %eax
     765: 48 85 c9                     	testq	%rcx, %rcx
     768: 78 cd                        	js	0x737 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x5d7>
     76a: 48 8b 86 e8 13 00 00         	movq	0x13e8(%rsi), %rax
     771: 48 39 86 f0 13 00 00         	cmpq	%rax, 0x13f0(%rsi)
     778: 0f 84 48 0b 00 00            	je	0x12c6 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1166>
     77e: 48 69 c9 50 20 00 00         	imulq	$0x2050, %rcx, %rcx     # imm = 0x2050
     785: 49 03 4c 24 10               	addq	0x10(%r12), %rcx
     78a: 48 ff c0                     	incq	%rax
     78d: 48 89 86 e8 13 00 00         	movq	%rax, 0x13e8(%rsi)
     794: c7 84 24 04 01 00 00 00 00 00 00     	movl	$0x0, 0x104(%rsp)
     79f: 48 8d 94 24 90 00 00 00      	leaq	0x90(%rsp), %rdx
     7a7: 4c 8d 84 24 04 01 00 00      	leaq	0x104(%rsp), %r8
     7af: e8 00 00 00 00               	callq	0x7b4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x654>
     7b4: 84 c0                        	testb	%al, %al
     7b6: 0f 84 74 ff ff ff            	je	0x730 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x5d0>
     7bc: 48 8b bc 24 a0 00 00 00      	movq	0xa0(%rsp), %rdi
     7c4: 89 c3                        	movl	%eax, %ebx
     7c6: e9 ff 0a 00 00               	jmp	0x12ca <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x116a>
     7cb: 0f 1f 44 00 00               	nopl	(%rax,%rax)
     7d0: 48 8b 84 24 18 01 00 00      	movq	0x118(%rsp), %rax
     7d8: 0f b6 3c c1                  	movzbl	(%rcx,%rax,8), %edi
     7dc: 48 c7 44 24 70 00 00 00 00   	movq	$0x0, 0x70(%rsp)
     7e5: 8b 93 f8 12 00 00            	movl	0x12f8(%rbx), %edx
     7eb: 4c 89 f9                     	movq	%r15, %rcx
     7ee: 4c 8d 44 24 70               	leaq	0x70(%rsp), %r8
     7f3: e8 00 00 00 00               	callq	0x7f8 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x698>
     7f8: 84 c0                        	testb	%al, %al
     7fa: 0f 84 70 0b 00 00            	je	0x1370 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1210>
     800: 48 8b 44 24 70               	movq	0x70(%rsp), %rax
     805: 48 89 44 24 68               	movq	%rax, 0x68(%rsp)
     80a: 48 85 c0                     	testq	%rax, %rax
     80d: 0f 84 9d fc ff ff            	je	0x4b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x350>
     813: 0f b6 8b f8 12 00 00         	movzbl	0x12f8(%rbx), %ecx
     81a: 49 c7 c6 ff ff ff ff         	movq	$-0x1, %r14
     821: 49 d3 e6                     	shlq	%cl, %r14
     824: f2 44 0f 10 74 24 68         	movsd	0x68(%rsp), %xmm14
     82b: 44 0f 14 35 00 00 00 00      	unpcklps	(%rip), %xmm14          # xmm14 = xmm14[0],mem[0],xmm14[1],mem[1]
                                                                        # 0x833 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x6d3>
     833: 66 45 0f 5c f0               	subpd	%xmm8, %xmm14
     838: 44 8b ab fc 12 00 00         	movl	0x12fc(%rbx), %r13d
     83f: 8b 83 00 13 00 00            	movl	0x1300(%rbx), %eax
     845: 0f bd d0                     	bsrl	%eax, %edx
     848: ff c2                        	incl	%edx
     84a: 85 c0                        	testl	%eax, %eax
     84c: 0f 44 d0                     	cmovel	%eax, %edx
     84f: c7 84 24 08 01 00 00 00 00 00 00     	movl	$0x0, 0x108(%rsp)
     85a: 4c 89 f9                     	movq	%r15, %rcx
     85d: 4c 8d 84 24 08 01 00 00      	leaq	0x108(%rsp), %r8
     865: e8 00 00 00 00               	callq	0x86a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x70a>
     86a: 84 c0                        	testb	%al, %al
     86c: 0f 84 fe 0a 00 00            	je	0x1370 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1210>
     872: 40 88 7c 24 48               	movb	%dil, 0x48(%rsp)
     877: 8b 84 24 08 01 00 00         	movl	0x108(%rsp), %eax
     87e: 3b 83 00 13 00 00            	cmpl	0x1300(%rbx), %eax
     884: 0f 83 ee 0a 00 00            	jae	0x1378 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1218>
     88a: 83 bb ec 12 00 00 00         	cmpl	$0x0, 0x12ec(%rbx)
     891: 48 8b be e8 13 00 00         	movq	0x13e8(%rsi), %rdi
     898: 0f 84 aa 07 00 00            	je	0x1048 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xee8>
     89e: 4c 89 ac 24 a8 00 00 00      	movq	%r13, 0xa8(%rsp)
     8a6: 4c 89 b4 24 88 00 00 00      	movq	%r14, 0x88(%rsp)
     8ae: 8b 84 83 04 13 00 00         	movl	0x1304(%rbx,%rax,4), %eax
     8b5: 4c 69 f0 50 20 00 00         	imulq	$0x2050, %rax, %r14     # imm = 0x2050
     8bc: 4d 03 74 24 10               	addq	0x10(%r12), %r14
     8c1: 4c 8b ae f0 13 00 00         	movq	0x13f0(%rsi), %r13
     8c8: 31 ed                        	xorl	%ebp, %ebp
     8ca: 66 45 0f 57 ff               	xorpd	%xmm15, %xmm15
     8cf: eb 2a                        	jmp	0x8fb <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x79b>
     8d1: 66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00 	nopw	%cs:(%rax,%rax)
     8e0: 8d 14 29                     	leal	(%rcx,%rbp), %edx
     8e3: ff ca                        	decl	%edx
     8e5: 01 e9                        	addl	%ebp, %ecx
     8e7: f2 44 0f 10 bc d6 e0 0b 00 00	movsd	0xbe0(%rsi,%rdx,8), %xmm15
     8f1: 39 c1                        	cmpl	%eax, %ecx
     8f3: 89 cd                        	movl	%ecx, %ebp
     8f5: 0f 83 d1 05 00 00            	jae	0xecc <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xd6c>
     8fb: 49 39 fd                     	cmpq	%rdi, %r13
     8fe: 0f 84 f0 09 00 00            	je	0x12f4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1194>
     904: 48 ff c7                     	incq	%rdi
     907: 48 89 be e8 13 00 00         	movq	%rdi, 0x13e8(%rsi)
     90e: c7 84 24 0c 01 00 00 00 00 00 00     	movl	$0x0, 0x10c(%rsp)
     919: 4c 89 f1                     	movq	%r14, %rcx
     91c: 4c 89 fa                     	movq	%r15, %rdx
     91f: 4c 8d 84 24 0c 01 00 00      	leaq	0x10c(%rsp), %r8
     927: e8 00 00 00 00               	callq	0x92c <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x7cc>
     92c: 84 c0                        	testb	%al, %al
     92e: 0f 85 d9 09 00 00            	jne	0x130d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11ad>
     934: 41 8b 16                     	movl	(%r14), %edx
     937: 8b 83 ec 12 00 00            	movl	0x12ec(%rbx), %eax
     93d: 89 c1                        	movl	%eax, %ecx
     93f: 29 e9                        	subl	%ebp, %ecx
     941: 39 d1                        	cmpl	%edx, %ecx
     943: 0f 43 ca                     	cmovael	%edx, %ecx
     946: 48 8b be e8 13 00 00         	movq	0x13e8(%rsi), %rdi
     94d: 4c 8b ae f0 13 00 00         	movq	0x13f0(%rsi), %r13
     954: 4d 89 e8                     	movq	%r13, %r8
     957: 49 29 f8                     	subq	%rdi, %r8
     95a: 49 39 c8                     	cmpq	%rcx, %r8
     95d: 0f 82 91 09 00 00            	jb	0x12f4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1194>
     963: 44 8b 84 24 0c 01 00 00      	movl	0x10c(%rsp), %r8d
     96b: 48 01 cf                     	addq	%rcx, %rdi
     96e: 48 89 be e8 13 00 00         	movq	%rdi, 0x13e8(%rsi)
     975: 48 85 c9                     	testq	%rcx, %rcx
     978: 0f 84 62 ff ff ff            	je	0x8e0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x780>
     97e: 49 0f af d0                  	imulq	%r8, %rdx
     982: 4d 8b 86 30 20 00 00         	movq	0x2030(%r14), %r8
     989: 4d 8d 0c d0                  	leaq	(%r8,%rdx,8), %r9
     98d: 83 f9 0e                     	cmpl	$0xe, %ecx
     990: 72 3e                        	jb	0x9d0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x870>
     992: 4c 8d 51 ff                  	leaq	-0x1(%rcx), %r10
     996: 41 89 eb                     	movl	%ebp, %r11d
     999: 45 01 d3                     	addl	%r10d, %r11d
     99c: 41 0f 92 c3                  	setb	%r11b
     9a0: 49 c1 ea 20                  	shrq	$0x20, %r10
     9a4: 41 0f 95 c2                  	setne	%r10b
     9a8: 45 08 da                     	orb	%r11b, %r10b
     9ab: 75 23                        	jne	0x9d0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x870>
     9ad: 41 89 eb                     	movl	%ebp, %r11d
     9b0: 4c 8b 94 24 e0 00 00 00      	movq	0xe0(%rsp), %r10
     9b8: 4f 8d 14 da                  	leaq	(%r10,%r11,8), %r10
     9bc: 4d 29 ca                     	subq	%r9, %r10
     9bf: 49 83 fa 20                  	cmpq	$0x20, %r10
     9c3: 0f 83 12 01 00 00            	jae	0xadb <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x97b>
     9c9: 0f 1f 80 00 00 00 00         	nopl	(%rax)
     9d0: 45 31 d2                     	xorl	%r10d, %r10d
     9d3: 48 89 cb                     	movq	%rcx, %rbx
     9d6: 4d 89 d3                     	movq	%r10, %r11
     9d9: 48 83 e3 03                  	andq	$0x3, %rbx
     9dd: 49 89 f4                     	movq	%rsi, %r12
     9e0: 74 30                        	je	0xa12 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x8b2>
     9e2: 89 ee                        	movl	%ebp, %esi
     9e4: c1 e3 03                     	shll	$0x3, %ebx
     9e7: 4d 89 d3                     	movq	%r10, %r11
     9ea: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     9f0: f2 43 0f 10 04 d9            	movsd	(%r9,%r11,8), %xmm0
     9f6: f2 41 0f 58 c7               	addsd	%xmm15, %xmm0
     9fb: 46 8d 3c 1e                  	leal	(%rsi,%r11), %r15d
     9ff: f2 43 0f 11 84 fc e0 0b 00 00	movsd	%xmm0, 0xbe0(%r12,%r15,8)
     a09: 49 ff c3                     	incq	%r11
     a0c: 48 83 c3 f8                  	addq	$-0x8, %rbx
     a10: 75 de                        	jne	0x9f0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x890>
     a12: 49 29 ca                     	subq	%rcx, %r10
     a15: 49 83 fa fc                  	cmpq	$-0x4, %r10
     a19: 4c 89 e6                     	movq	%r12, %rsi
     a1c: 4c 8b a4 24 d8 00 00 00      	movq	0xd8(%rsp), %r12
     a24: 4c 8d bc 24 90 00 00 00      	leaq	0x90(%rsp), %r15
     a2c: 48 8b 5c 24 38               	movq	0x38(%rsp), %rbx
     a31: 0f 87 a9 fe ff ff            	ja	0x8e0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x780>
     a37: 49 89 c9                     	movq	%rcx, %r9
     a3a: 4d 29 d9                     	subq	%r11, %r9
     a3d: 45 8d 14 2b                  	leal	(%r11,%rbp), %r10d
     a41: 49 c1 e3 03                  	shlq	$0x3, %r11
     a45: 49 8d 14 d3                  	leaq	(%r11,%rdx,8), %rdx
     a49: 4c 01 c2                     	addq	%r8, %rdx
     a4c: 48 83 c2 18                  	addq	$0x18, %rdx
     a50: 45 31 c0                     	xorl	%r8d, %r8d
     a53: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
     a60: f2 42 0f 10 44 c2 e8         	movsd	-0x18(%rdx,%r8,8), %xmm0
     a67: f2 41 0f 58 c7               	addsd	%xmm15, %xmm0
     a6c: 47 8d 1c 02                  	leal	(%r10,%r8), %r11d
     a70: f2 42 0f 11 84 de e0 0b 00 00	movsd	%xmm0, 0xbe0(%rsi,%r11,8)
     a7a: f2 42 0f 10 44 c2 f0         	movsd	-0x10(%rdx,%r8,8), %xmm0
     a81: f2 41 0f 58 c7               	addsd	%xmm15, %xmm0
     a86: 47 8d 5c 02 01               	leal	0x1(%r10,%r8), %r11d
     a8b: f2 42 0f 11 84 de e0 0b 00 00	movsd	%xmm0, 0xbe0(%rsi,%r11,8)
     a95: f2 42 0f 10 44 c2 f8         	movsd	-0x8(%rdx,%r8,8), %xmm0
     a9c: f2 41 0f 58 c7               	addsd	%xmm15, %xmm0
     aa1: 47 8d 5c 02 02               	leal	0x2(%r10,%r8), %r11d
     aa6: f2 42 0f 11 84 de e0 0b 00 00	movsd	%xmm0, 0xbe0(%rsi,%r11,8)
     ab0: f2 42 0f 10 04 c2            	movsd	(%rdx,%r8,8), %xmm0
     ab6: f2 41 0f 58 c7               	addsd	%xmm15, %xmm0
     abb: 47 8d 1c 02                  	leal	(%r10,%r8), %r11d
     abf: 41 83 c3 03                  	addl	$0x3, %r11d
     ac3: f2 42 0f 11 84 de e0 0b 00 00	movsd	%xmm0, 0xbe0(%rsi,%r11,8)
     acd: 49 83 c0 04                  	addq	$0x4, %r8
     ad1: 4d 39 c1                     	cmpq	%r8, %r9
     ad4: 75 8a                        	jne	0xa60 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x900>
     ad6: e9 05 fe ff ff               	jmp	0x8e0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x780>
     adb: 49 89 f4                     	movq	%rsi, %r12
     ade: 48 8d 34 d5 00 00 00 00      	leaq	(,%rdx,8), %rsi
     ae6: 41 89 ca                     	movl	%ecx, %r10d
     ae9: 41 83 e2 fc                  	andl	$-0x4, %r10d
     aed: 66 41 0f 28 c7               	movapd	%xmm15, %xmm0
     af2: 66 41 0f 14 c7               	unpcklpd	%xmm15, %xmm0           # xmm0 = xmm0[0],xmm15[0]
     af7: 4c 01 c6                     	addq	%r8, %rsi
     afa: 48 83 c6 10                  	addq	$0x10, %rsi
     afe: 31 db                        	xorl	%ebx, %ebx
     b00: 66 0f 10 4c de f0            	movupd	-0x10(%rsi,%rbx,8), %xmm1
     b06: 66 0f 10 14 de               	movupd	(%rsi,%rbx,8), %xmm2
     b0b: 66 0f 58 c8                  	addpd	%xmm0, %xmm1
     b0f: 66 0f 58 d0                  	addpd	%xmm0, %xmm2
     b13: 45 8d 3c 1b                  	leal	(%r11,%rbx), %r15d
     b17: 66 43 0f 11 8c fc e0 0b 00 00	movupd	%xmm1, 0xbe0(%r12,%r15,8)
     b21: 66 43 0f 11 94 fc f0 0b 00 00	movupd	%xmm2, 0xbf0(%r12,%r15,8)
     b2b: 48 83 c3 04                  	addq	$0x4, %rbx
     b2f: 49 39 da                     	cmpq	%rbx, %r10
     b32: 75 cc                        	jne	0xb00 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x9a0>
     b34: 41 39 ca                     	cmpl	%ecx, %r10d
     b37: 4c 89 e6                     	movq	%r12, %rsi
     b3a: 4c 8b a4 24 d8 00 00 00      	movq	0xd8(%rsp), %r12
     b42: 4c 8d bc 24 90 00 00 00      	leaq	0x90(%rsp), %r15
     b4a: 48 8b 5c 24 38               	movq	0x38(%rsp), %rbx
     b4f: 0f 84 8b fd ff ff            	je	0x8e0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x780>
     b55: e9 79 fe ff ff               	jmp	0x9d3 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x873>
     b5a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
     b60: 48 8b 84 24 80 00 00 00      	movq	0x80(%rsp), %rax
     b68: 66 0f 11 b0 ea 00 00 00      	movupd	%xmm6, 0xea(%rax)
     b70: 66 0f 11 b0 e0 00 00 00      	movupd	%xmm6, 0xe0(%rax)
     b78: 66 0f 11 b0 d0 00 00 00      	movupd	%xmm6, 0xd0(%rax)
     b80: 66 0f 11 b0 c0 00 00 00      	movupd	%xmm6, 0xc0(%rax)
     b88: 66 0f 11 b0 b0 00 00 00      	movupd	%xmm6, 0xb0(%rax)
     b90: 66 0f 11 b0 a0 00 00 00      	movupd	%xmm6, 0xa0(%rax)
     b98: 66 0f 11 b0 90 00 00 00      	movupd	%xmm6, 0x90(%rax)
     ba0: 66 0f 11 b0 80 00 00 00      	movupd	%xmm6, 0x80(%rax)
     ba8: 66 0f 11 70 70               	movupd	%xmm6, 0x70(%rax)
     bad: 66 0f 11 70 60               	movupd	%xmm6, 0x60(%rax)
     bb2: 66 0f 11 70 50               	movupd	%xmm6, 0x50(%rax)
     bb7: 66 0f 11 70 40               	movupd	%xmm6, 0x40(%rax)
     bbc: 66 0f 11 70 30               	movupd	%xmm6, 0x30(%rax)
     bc1: 66 0f 11 70 20               	movupd	%xmm6, 0x20(%rax)
     bc6: 66 0f 11 70 10               	movupd	%xmm6, 0x10(%rax)
     bcb: 66 0f 11 30                  	movupd	%xmm6, (%rax)
     bcf: 66 c7 86 e4 0a 00 00 01 01   	movw	$0x101, 0xae4(%rsi)     # imm = 0x101
     bd8: 8b 83 48 03 00 00            	movl	0x348(%rbx), %eax
     bde: 83 f8 03                     	cmpl	$0x3, %eax
     be1: 0f 82 4c 01 00 00            	jb	0xd33 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xbd3>
     be7: 48 8b 44 24 68               	movq	0x68(%rsp), %rax
     bec: 48 8b 4c 24 48               	movq	0x48(%rsp), %rcx
     bf1: 48 01 c1                     	addq	%rax, %rcx
     bf4: 48 81 c1 24 0b 00 00         	addq	$0xb24, %rcx            # imm = 0xB24
     bfb: 45 31 c0                     	xorl	%r8d, %r8d
     bfe: 4c 8b b4 24 a8 00 00 00      	movq	0xa8(%rsp), %r14
     c06: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
     c10: 46 8b 8c 81 18 fc ff ff      	movl	-0x3e8(%rcx,%r8,4), %r9d
     c18: 46 8b 14 81                  	movl	(%rcx,%r8,4), %r10d
     c1c: 42 8b 84 81 30 f8 ff ff      	movl	-0x7d0(%rcx,%r8,4), %eax
     c24: 42 8b 94 8b 4c 03 00 00      	movl	0x34c(%rbx,%r9,4), %edx
     c2c: 46 8b 9c 93 4c 03 00 00      	movl	0x34c(%rbx,%r10,4), %r11d
     c34: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
     c39: 42 8b b4 8e fc 06 00 00      	movl	0x6fc(%rsi,%r9,4), %esi
     c41: 48 8b 7c 24 60               	movq	0x60(%rsp), %rdi
     c46: 42 8b bc 97 fc 06 00 00      	movl	0x6fc(%rdi,%r10,4), %edi
     c4e: 89 fb                        	movl	%edi, %ebx
     c50: 29 f3                        	subl	%esi, %ebx
     c52: 89 f5                        	movl	%esi, %ebp
     c54: 29 fd                        	subl	%edi, %ebp
     c56: 0f 4c eb                     	cmovll	%ebx, %ebp
     c59: 29 d0                        	subl	%edx, %eax
     c5b: 0f af c5                     	imull	%ebp, %eax
     c5e: 41 29 d3                     	subl	%edx, %r11d
     c61: 99                           	cltd
     c62: 41 f7 fb                     	idivl	%r11d
     c65: 89 c2                        	movl	%eax, %edx
     c67: f7 da                        	negl	%edx
     c69: 39 f7                        	cmpl	%esi, %edi
     c6b: 0f 49 d0                     	cmovnsl	%eax, %edx
     c6e: 01 f2                        	addl	%esi, %edx
     c70: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
     c75: 42 8b 84 86 04 07 00 00      	movl	0x704(%rsi,%r8,4), %eax
     c7d: 85 c0                        	testl	%eax, %eax
     c7f: 74 4f                        	je	0xcd0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xb70>
     c81: 45 89 f3                     	movl	%r14d, %r11d
     c84: 41 29 d3                     	subl	%edx, %r11d
     c87: 44 39 da                     	cmpl	%r11d, %edx
     c8a: 48 89 f7                     	movq	%rsi, %rdi
     c8d: 44 89 de                     	movl	%r11d, %esi
     c90: 0f 4c f2                     	cmovll	%edx, %esi
     c93: 01 f6                        	addl	%esi, %esi
     c95: 42 c6 84 0f e4 0a 00 00 01   	movb	$0x1, 0xae4(%rdi,%r9)
     c9e: 42 c6 84 17 e4 0a 00 00 01   	movb	$0x1, 0xae4(%rdi,%r10)
     ca7: 42 c6 84 07 e6 0a 00 00 01   	movb	$0x1, 0xae6(%rdi,%r8)
     cb0: 39 f0                        	cmpl	%esi, %eax
     cb2: 48 8b 5c 24 38               	movq	0x38(%rsp), %rbx
     cb7: 7d 27                        	jge	0xce0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xb80>
     cb9: a8 01                        	testb	$0x1, %al
     cbb: 75 38                        	jne	0xcf5 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xb95>
     cbd: d1 f8                        	sarl	%eax
     cbf: 01 c2                        	addl	%eax, %edx
     cc1: eb 38                        	jmp	0xcfb <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xb9b>
     cc3: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
     cd0: 48 8b 5c 24 38               	movq	0x38(%rsp), %rbx
     cd5: eb 29                        	jmp	0xd00 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xba0>
     cd7: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
     ce0: 41 89 c1                     	movl	%eax, %r9d
     ce3: 41 f7 d1                     	notl	%r9d
     ce6: 45 01 f1                     	addl	%r14d, %r9d
     ce9: 44 39 da                     	cmpl	%r11d, %edx
     cec: 44 0f 4c c8                  	cmovll	%eax, %r9d
     cf0: 44 89 ca                     	movl	%r9d, %edx
     cf3: eb 06                        	jmp	0xcfb <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xb9b>
     cf5: ff c0                        	incl	%eax
     cf7: d1 f8                        	sarl	%eax
     cf9: 29 c2                        	subl	%eax, %edx
     cfb: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
     d00: 85 d2                        	testl	%edx, %edx
     d02: 0f 88 11 06 00 00            	js	0x1319 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11b9>
     d08: 44 39 f2                     	cmpl	%r14d, %edx
     d0b: 0f 8d 08 06 00 00            	jge	0x1319 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11b9>
     d11: 42 89 94 86 04 07 00 00      	movl	%edx, 0x704(%rsi,%r8,4)
     d19: 8b 83 48 03 00 00            	movl	0x348(%rbx), %eax
     d1f: 49 8d 50 01                  	leaq	0x1(%r8), %rdx
     d23: 49 83 c0 03                  	addq	$0x3, %r8
     d27: 49 39 c0                     	cmpq	%rax, %r8
     d2a: 49 89 d0                     	movq	%rdx, %r8
     d2d: 0f 82 dd fe ff ff            	jb	0xc10 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xab0>
     d33: 44 8b 93 44 03 00 00         	movl	0x344(%rbx), %r10d
     d3a: 44 8b 86 fc 06 00 00         	movl	0x6fc(%rsi), %r8d
     d41: 45 0f af c2                  	imull	%r10d, %r8d
     d45: 83 f8 02                     	cmpl	$0x2, %eax
     d48: 0f 82 1b 02 00 00            	jb	0xf69 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xe09>
     d4e: 41 89 c3                     	movl	%eax, %r11d
     d51: 48 8b 44 24 78               	movq	0x78(%rsp), %rax
     d56: 48 0f af 44 24 58            	imulq	0x58(%rsp), %rax
     d5c: 48 03 84 24 c0 00 00 00      	addq	0xc0(%rsp), %rax
     d64: 48 89 44 24 48               	movq	%rax, 0x48(%rsp)
     d69: 45 31 ed                     	xorl	%r13d, %r13d
     d6c: bf 01 00 00 00               	movl	$0x1, %edi
     d71: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
     d76: 44 89 94 24 88 00 00 00      	movl	%r10d, 0x88(%rsp)
     d7e: 4c 89 9c 24 a8 00 00 00      	movq	%r11, 0xa8(%rsp)
     d86: eb 2c                        	jmp	0xdb4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xc54>
     d88: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
     d90: 45 89 f5                     	movl	%r14d, %r13d
     d93: 41 89 d8                     	movl	%ebx, %r8d
     d96: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
     d9b: 4c 8d bc 24 90 00 00 00      	leaq	0x90(%rsp), %r15
     da3: 48 8b 5c 24 38               	movq	0x38(%rsp), %rbx
     da8: 48 ff c7                     	incq	%rdi
     dab: 4c 39 df                     	cmpq	%r11, %rdi
     dae: 0f 84 c3 01 00 00            	je	0xf77 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xe17>
     db4: 8b 84 bb 04 0f 00 00         	movl	0xf04(%rbx,%rdi,4), %eax
     dbb: 48 8b 8c 24 80 00 00 00      	movq	0x80(%rsp), %rcx
     dc3: 80 3c 01 01                  	cmpb	$0x1, (%rcx,%rax)
     dc7: 75 df                        	jne	0xda8 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xc48>
     dc9: 48 89 d9                     	movq	%rbx, %rcx
     dcc: 8b 9c 86 fc 06 00 00         	movl	0x6fc(%rsi,%rax,4), %ebx
     dd3: 41 0f af da                  	imull	%r10d, %ebx
     dd7: 44 8b b4 81 4c 03 00 00      	movl	0x34c(%rcx,%rax,4), %r14d
     ddf: 49 89 ef                     	movq	%rbp, %r15
     de2: 44 89 f5                     	movl	%r14d, %ebp
     de5: 44 29 ed                     	subl	%r13d, %ebp
     de8: 89 d9                        	movl	%ebx, %ecx
     dea: 44 29 c1                     	subl	%r8d, %ecx
     ded: 89 c8                        	movl	%ecx, %eax
     def: 99                           	cltd
     df0: f7 fd                        	idivl	%ebp
     df2: 44 89 c2                     	movl	%r8d, %edx
     df5: 29 da                        	subl	%ebx, %edx
     df7: 0f 4c d1                     	cmovll	%ecx, %edx
     dfa: 41 89 c1                     	movl	%eax, %r9d
     dfd: 41 f7 d9                     	negl	%r9d
     e00: 44 0f 48 c8                  	cmovsl	%eax, %r9d
     e04: 45 39 fe                     	cmpl	%r15d, %r14d
     e07: 45 0f 4c fe                  	cmovll	%r14d, %r15d
     e0b: 45 29 ef                     	subl	%r13d, %r15d
     e0e: 7e 80                        	jle	0xd90 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xc30>
     e10: 48 89 7c 24 68               	movq	%rdi, 0x68(%rsp)
     e15: c1 f9 1f                     	sarl	$0x1f, %ecx
     e18: 83 c9 01                     	orl	$0x1, %ecx
     e1b: 44 0f af cd                  	imull	%ebp, %r9d
     e1f: 44 29 ca                     	subl	%r9d, %edx
     e22: 4d 63 cd                     	movslq	%r13d, %r9
     e25: 4c 8b 54 24 48               	movq	0x48(%rsp), %r10
     e2a: 4f 8d 2c ca                  	leaq	(%r10,%r9,8), %r13
     e2e: 45 31 db                     	xorl	%r11d, %r11d
     e31: 45 31 c9                     	xorl	%r9d, %r9d
     e34: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
     e40: 41 81 f8 ff 00 00 00         	cmpl	$0xff, %r8d
     e47: 0f 87 91 04 00 00            	ja	0x12de <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x117e>
     e4d: 44 89 c6                     	movl	%r8d, %esi
     e50: f2 41 0f 10 84 f4 c0 02 00 00	movsd	0x2c0(%r12,%rsi,8), %xmm0
     e5a: f2 43 0f 11 44 cd 00         	movsd	%xmm0, (%r13,%r9,8)
     e61: 41 01 d3                     	addl	%edx, %r11d
     e64: 41 39 eb                     	cmpl	%ebp, %r11d
     e67: 89 ce                        	movl	%ecx, %esi
     e69: 41 ba 00 00 00 00            	movl	$0x0, %r10d
     e6f: 41 0f 4c f2                  	cmovll	%r10d, %esi
     e73: 89 df                        	movl	%ebx, %edi
     e75: 44 89 f3                     	movl	%r14d, %ebx
     e78: 41 89 ee                     	movl	%ebp, %r14d
     e7b: 45 0f 4c f2                  	cmovll	%r10d, %r14d
     e7f: 45 29 f3                     	subl	%r14d, %r11d
     e82: 41 89 de                     	movl	%ebx, %r14d
     e85: 89 fb                        	movl	%edi, %ebx
     e87: 41 01 c0                     	addl	%eax, %r8d
     e8a: 41 01 f0                     	addl	%esi, %r8d
     e8d: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
     e92: 49 ff c1                     	incq	%r9
     e95: 45 39 cf                     	cmpl	%r9d, %r15d
     e98: 75 a6                        	jne	0xe40 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xce0>
     e9a: 45 89 f5                     	movl	%r14d, %r13d
     e9d: 41 89 d8                     	movl	%ebx, %r8d
     ea0: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
     ea5: 4c 8d bc 24 90 00 00 00      	leaq	0x90(%rsp), %r15
     ead: 48 8b 5c 24 38               	movq	0x38(%rsp), %rbx
     eb2: 44 8b 94 24 88 00 00 00      	movl	0x88(%rsp), %r10d
     eba: 4c 8b 9c 24 a8 00 00 00      	movq	0xa8(%rsp), %r11
     ec2: 48 8b 7c 24 68               	movq	0x68(%rsp), %rdi
     ec7: e9 dc fe ff ff               	jmp	0xda8 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xc48>
     ecc: 85 c0                        	testl	%eax, %eax
     ece: 0f 84 5f 01 00 00            	je	0x1033 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xed3>
     ed4: 49 89 de                     	movq	%rbx, %r14
     ed7: 48 89 f3                     	movq	%rsi, %rbx
     eda: 31 f6                        	xorl	%esi, %esi
     edc: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
     ee1: 66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00 	nopw	%cs:(%rax,%rax)
     ef0: f2 0f 10 84 f3 e0 0b 00 00   	movsd	0xbe0(%rbx,%rsi,8), %xmm0
     ef9: e8 00 00 00 00               	callq	0xefe <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xd9e>
     efe: f2 0f 58 c0                  	addsd	%xmm0, %xmm0
     f02: f2 0f 11 84 f3 e0 0b 00 00   	movsd	%xmm0, 0xbe0(%rbx,%rsi,8)
     f0b: 48 ff c6                     	incq	%rsi
     f0e: 41 8b 8e ec 12 00 00         	movl	0x12ec(%r14), %ecx
     f15: 48 39 ce                     	cmpq	%rcx, %rsi
     f18: 72 d6                        	jb	0xef0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xd90>
     f1a: 48 0f af cd                  	imulq	%rbp, %rcx
     f1e: 49 29 fd                     	subq	%rdi, %r13
     f21: 4c 39 e9                     	cmpq	%r13, %rcx
     f24: 0f 87 63 04 00 00            	ja	0x138d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x122d>
     f2a: 0f b6 44 24 48               	movzbl	0x48(%rsp), %eax
     f2f: c1 e0 05                     	shll	$0x5, %eax
     f32: 48 0d 48 13 00 00            	orq	$0x1348, %rax           # imm = 0x1348
     f38: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
     f3d: 48 8b 5c 24 38               	movq	0x38(%rsp), %rbx
     f42: 4c 8b b4 24 88 00 00 00      	movq	0x88(%rsp), %r14
     f4a: 4c 8b ac 24 a8 00 00 00      	movq	0xa8(%rsp), %r13
     f52: 48 01 f9                     	addq	%rdi, %rcx
     f55: 48 89 8e e8 13 00 00         	movq	%rcx, 0x13e8(%rsi)
     f5c: 85 ed                        	testl	%ebp, %ebp
     f5e: 0f 85 06 01 00 00            	jne	0x106a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xf0a>
     f64: e9 47 f5 ff ff               	jmp	0x4b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x350>
     f69: 31 c9                        	xorl	%ecx, %ecx
     f6b: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
     f70: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
     f75: eb 08                        	jmp	0xf7f <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xe1f>
     f77: 49 63 cd                     	movslq	%r13d, %rcx
     f7a: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
     f7f: 41 81 f8 ff 00 00 00         	cmpl	$0xff, %r8d
     f86: 4c 8b 9c 24 c0 00 00 00      	movq	0xc0(%rsp), %r11
     f8e: 0f 87 e0 03 00 00            	ja	0x1374 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1214>
     f94: b0 01                        	movb	$0x1, %al
     f96: 39 cd                        	cmpl	%ecx, %ebp
     f98: 0f 86 20 f5 ff ff            	jbe	0x4be <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x35e>
     f9e: 44 89 c2                     	movl	%r8d, %edx
     fa1: f2 41 0f 10 84 d4 c0 02 00 00	movsd	0x2c0(%r12,%rdx,8), %xmm0
     fab: 48 89 ea                     	movq	%rbp, %rdx
     fae: 48 29 ca                     	subq	%rcx, %rdx
     fb1: 48 83 fa 04                  	cmpq	$0x4, %rdx
     fb5: 72 58                        	jb	0x100f <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xeaf>
     fb7: 41 89 d0                     	movl	%edx, %r8d
     fba: 41 83 e0 fc                  	andl	$-0x4, %r8d
     fbe: 66 0f 28 c8                  	movapd	%xmm0, %xmm1
     fc2: 66 0f 14 c8                  	unpcklpd	%xmm0, %xmm1            # xmm1 = xmm1[0],xmm0[0]
     fc6: 4c 8b 4c 24 78               	movq	0x78(%rsp), %r9
     fcb: 4c 0f af 4c 24 58            	imulq	0x58(%rsp), %r9
     fd1: 4d 8d 0c c9                  	leaq	(%r9,%rcx,8), %r9
     fd5: 4c 01 c1                     	addq	%r8, %rcx
     fd8: 4d 01 d9                     	addq	%r11, %r9
     fdb: 49 83 c1 10                  	addq	$0x10, %r9
     fdf: 45 31 d2                     	xorl	%r10d, %r10d
     fe2: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
     ff0: 66 43 0f 11 4c d1 f0         	movupd	%xmm1, -0x10(%r9,%r10,8)
     ff7: 66 43 0f 11 0c d1            	movupd	%xmm1, (%r9,%r10,8)
     ffd: 49 83 c2 04                  	addq	$0x4, %r10
    1001: 4d 39 d0                     	cmpq	%r10, %r8
    1004: 75 ea                        	jne	0xff0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xe90>
    1006: 44 39 c2                     	cmpl	%r8d, %edx
    1009: 0f 84 af f4 ff ff            	je	0x4be <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x35e>
    100f: 48 8b 54 24 58               	movq	0x58(%rsp), %rdx
    1014: 48 0f af 54 24 78            	imulq	0x78(%rsp), %rdx
    101a: 49 01 d3                     	addq	%rdx, %r11
    101d: 0f 1f 00                     	nopl	(%rax)
    1020: f2 41 0f 11 04 cb            	movsd	%xmm0, (%r11,%rcx,8)
    1026: 48 ff c1                     	incq	%rcx
    1029: 48 39 cd                     	cmpq	%rcx, %rbp
    102c: 75 f2                        	jne	0x1020 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xec0>
    102e: e9 8b f4 ff ff               	jmp	0x4be <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x35e>
    1033: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
    1038: 4c 8b b4 24 88 00 00 00      	movq	0x88(%rsp), %r14
    1040: 4c 8b ac 24 a8 00 00 00      	movq	0xa8(%rsp), %r13
    1048: 0f b6 44 24 48               	movzbl	0x48(%rsp), %eax
    104d: c1 e0 05                     	shll	$0x5, %eax
    1050: 48 0d 48 13 00 00            	orq	$0x1348, %rax           # imm = 0x1348
    1056: 31 c9                        	xorl	%ecx, %ecx
    1058: 48 01 f9                     	addq	%rdi, %rcx
    105b: 48 89 8e e8 13 00 00         	movq	%rcx, 0x13e8(%rsi)
    1062: 85 ed                        	testl	%ebp, %ebp
    1064: 0f 84 46 f4 ff ff            	je	0x4b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x350>
    106a: 48 8b 54 24 58               	movq	0x58(%rsp), %rdx
    106f: 48 0f af 94 24 b8 00 00 00   	imulq	0xb8(%rsp), %rdx
    1078: 66 41 0f 28 c6               	movapd	%xmm14, %xmm0
    107d: 66 41 0f 15 c6               	unpckhpd	%xmm14, %xmm0           # xmm0 = xmm0[1],xmm14[1]
    1082: f2 41 0f 58 c6               	addsd	%xmm14, %xmm0
    1087: 45 0f 57 f6                  	xorps	%xmm14, %xmm14
    108b: f2 4d 0f 2a f5               	cvtsi2sd	%r13, %xmm14
    1090: 49 f7 d6                     	notq	%r14
    1093: f2 44 0f 59 f0               	mulsd	%xmm0, %xmm14
    1098: 0f 57 c0                     	xorps	%xmm0, %xmm0
    109b: f2 49 0f 2a c6               	cvtsi2sd	%r14, %xmm0
    10a0: 48 8b 8c 24 c0 00 00 00      	movq	0xc0(%rsp), %rcx
    10a8: 48 8d 3c d1                  	leaq	(%rcx,%rdx,8), %rdi
    10ac: f2 44 0f 5e f0               	divsd	%xmm0, %xmm14
    10b1: 4c 8b 34 03                  	movq	(%rbx,%rax), %r14
    10b5: 45 31 ed                     	xorl	%r13d, %r13d
    10b8: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
    10c0: f2 43 0f 10 3c ee            	movsd	(%r14,%r13,8), %xmm7
    10c6: 8b 83 ec 12 00 00            	movl	0x12ec(%rbx), %eax
    10cc: b9 01 00 00 00               	movl	$0x1, %ecx
    10d1: 66 41 0f 28 d1               	movapd	%xmm9, %xmm2
    10d6: 48 83 f8 02                  	cmpq	$0x2, %rax
    10da: 0f 82 be 00 00 00            	jb	0x119e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x103e>
    10e0: 66 0f 28 cf                  	movapd	%xmm7, %xmm1
    10e4: 66 0f 14 cf                  	unpcklpd	%xmm7, %xmm1            # xmm1 = xmm1[0],xmm7[0]
    10e8: 4c 8d 48 fe                  	leaq	-0x2(%rax), %r9
    10ec: 4d 89 c8                     	movq	%r9, %r8
    10ef: 49 d1 e8                     	shrq	%r8
    10f2: 49 ff c0                     	incq	%r8
    10f5: 44 89 c2                     	movl	%r8d, %edx
    10f8: 83 e2 03                     	andl	$0x3, %edx
    10fb: b9 01 00 00 00               	movl	$0x1, %ecx
    1100: 66 41 0f 28 d1               	movapd	%xmm9, %xmm2
    1105: 49 83 f9 06                  	cmpq	$0x6, %r9
    1109: 72 75                        	jb	0x1180 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1020>
    110b: 49 83 e0 fc                  	andq	$-0x4, %r8
    110f: b9 82 01 00 00               	movl	$0x182, %ecx            # imm = 0x182
    1114: 66 41 0f 28 d1               	movapd	%xmm9, %xmm2
    1119: 0f 1f 80 00 00 00 00         	nopl	(%rax)
    1120: 66 0f 10 5c ce d0            	movupd	-0x30(%rsi,%rcx,8), %xmm3
    1126: 66 0f 10 64 ce e0            	movupd	-0x20(%rsi,%rcx,8), %xmm4
    112c: 66 0f 10 6c ce f0            	movupd	-0x10(%rsi,%rcx,8), %xmm5
    1132: 66 44 0f 10 3c ce            	movupd	(%rsi,%rcx,8), %xmm15
    1138: 66 0f 28 c1                  	movapd	%xmm1, %xmm0
    113c: 66 0f 5c c3                  	subpd	%xmm3, %xmm0
    1140: 66 0f 59 c2                  	mulpd	%xmm2, %xmm0
    1144: 66 0f 28 d1                  	movapd	%xmm1, %xmm2
    1148: 66 0f 5c d4                  	subpd	%xmm4, %xmm2
    114c: 66 0f 59 d0                  	mulpd	%xmm0, %xmm2
    1150: 66 0f 28 c1                  	movapd	%xmm1, %xmm0
    1154: 66 0f 5c c5                  	subpd	%xmm5, %xmm0
    1158: 66 0f 59 c2                  	mulpd	%xmm2, %xmm0
    115c: 66 0f 28 d1                  	movapd	%xmm1, %xmm2
    1160: 66 41 0f 5c d7               	subpd	%xmm15, %xmm2
    1165: 66 0f 59 d0                  	mulpd	%xmm0, %xmm2
    1169: 48 83 c1 08                  	addq	$0x8, %rcx
    116d: 49 83 c0 fc                  	addq	$-0x4, %r8
    1171: 75 ad                        	jne	0x1120 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xfc0>
    1173: 48 81 c1 7f fe ff ff         	addq	$-0x181, %rcx           # imm = 0xFE7F
    117a: 48 85 d2                     	testq	%rdx, %rdx
    117d: 74 1f                        	je	0x119e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x103e>
    117f: 90                           	nop
    1180: 66 0f 10 84 ce d8 0b 00 00   	movupd	0xbd8(%rsi,%rcx,8), %xmm0
    1189: 66 0f 28 d9                  	movapd	%xmm1, %xmm3
    118d: 66 0f 5c d8                  	subpd	%xmm0, %xmm3
    1191: 66 0f 59 d3                  	mulpd	%xmm3, %xmm2
    1195: 48 83 c1 02                  	addq	$0x2, %rcx
    1199: 48 ff ca                     	decq	%rdx
    119c: 75 e2                        	jne	0x1180 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1020>
    119e: 39 c1                        	cmpl	%eax, %ecx
    11a0: 75 2e                        	jne	0x11d0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1070>
    11a2: ff c8                        	decl	%eax
    11a4: 66 0f 28 df                  	movapd	%xmm7, %xmm3
    11a8: f2 0f 5c 9c c6 e0 0b 00 00   	subsd	0xbe0(%rsi,%rax,8), %xmm3
    11b1: f2 0f 59 da                  	mulsd	%xmm2, %xmm3
    11b5: f2 0f 59 ff                  	mulsd	%xmm7, %xmm7
    11b9: 66 41 0f 28 c2               	movapd	%xmm10, %xmm0
    11be: f2 0f 5c c7                  	subsd	%xmm7, %xmm0
    11c2: f2 0f 59 db                  	mulsd	%xmm3, %xmm3
    11c6: eb 22                        	jmp	0x11ea <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x108a>
    11c8: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
    11d0: 66 41 0f 28 c5               	movapd	%xmm13, %xmm0
    11d5: f2 0f 5c c7                  	subsd	%xmm7, %xmm0
    11d9: f2 41 0f 58 fd               	addsd	%xmm13, %xmm7
    11de: f2 0f 59 fa                  	mulsd	%xmm2, %xmm7
    11e2: f2 0f 59 fa                  	mulsd	%xmm2, %xmm7
    11e6: 66 0f 28 df                  	movapd	%xmm7, %xmm3
    11ea: 66 0f 15 d2                  	unpckhpd	%xmm2, %xmm2            # xmm2 = xmm2[1,1]
    11ee: f2 0f 59 c2                  	mulsd	%xmm2, %xmm0
    11f2: f2 0f 59 c2                  	mulsd	%xmm2, %xmm0
    11f6: f2 0f 58 c3                  	addsd	%xmm3, %xmm0
    11fa: 66 48 0f 7e c0               	movq	%xmm0, %rax
    11ff: 48 85 c0                     	testq	%rax, %rax
    1202: 0f 98 c1                     	sets	%cl
    1205: 48 89 c2                     	movq	%rax, %rdx
    1208: 49 b8 ff ff ff ff ff ff ff 7f	movabsq	$0x7fffffffffffffff, %r8 # imm = 0x7FFFFFFFFFFFFFFF
    1212: 4c 21 c2                     	andq	%r8, %rdx
    1215: 49 b8 00 00 00 00 00 00 f0 ff	movabsq	$-0x10000000000000, %r8 # imm = 0xFFF0000000000000
    121f: 4c 01 c2                     	addq	%r8, %rdx
    1222: 48 c1 ea 35                  	shrq	$0x35, %rdx
    1226: 81 fa ff 03 00 00            	cmpl	$0x3ff, %edx            # imm = 0x3FF
    122c: 0f 93 c2                     	setae	%dl
    122f: 08 ca                        	orb	%cl, %dl
    1231: 48 ff c8                     	decq	%rax
    1234: 48 b9 fe ff ff ff ff ff 0f 00	movabsq	$0xffffffffffffe, %rcx  # imm = 0xFFFFFFFFFFFFE
    123e: 48 39 c8                     	cmpq	%rcx, %rax
    1241: 0f 97 c0                     	seta	%al
    1244: 84 d0                        	testb	%dl, %al
    1246: 0f 85 b8 00 00 00            	jne	0x1304 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11a4>
    124c: 66 41 0f 2e c3               	ucomisd	%xmm11, %xmm0
    1251: 72 0d                        	jb	0x1260 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1100>
    1253: f2 0f 51 c0                  	sqrtsd	%xmm0, %xmm0
    1257: eb 0c                        	jmp	0x1265 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1105>
    1259: 0f 1f 80 00 00 00 00         	nopl	(%rax)
    1260: e8 00 00 00 00               	callq	0x1265 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1105>
    1265: 66 41 0f 28 ce               	movapd	%xmm14, %xmm1
    126a: f2 0f 5e c8                  	divsd	%xmm0, %xmm1
    126e: 8b 83 fc 12 00 00            	movl	0x12fc(%rbx), %eax
    1274: 0f 57 c0                     	xorps	%xmm0, %xmm0
    1277: f2 48 0f 2a c0               	cvtsi2sd	%rax, %xmm0
    127c: f2 0f 5c c8                  	subsd	%xmm0, %xmm1
    1280: f2 41 0f 59 cc               	mulsd	%xmm12, %xmm1
    1285: 66 0f 28 c1                  	movapd	%xmm1, %xmm0
    1289: e8 00 00 00 00               	callq	0x128e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x112e>
    128e: 66 48 0f 7e c0               	movq	%xmm0, %rax
    1293: 48 b9 ff ff ff ff ff ff ff 7f	movabsq	$0x7fffffffffffffff, %rcx # imm = 0x7FFFFFFFFFFFFFFF
    129d: 48 21 c8                     	andq	%rcx, %rax
    12a0: 48 b9 00 00 00 00 00 00 f0 7f	movabsq	$0x7ff0000000000000, %rcx # imm = 0x7FF0000000000000
    12aa: 48 39 c8                     	cmpq	%rcx, %rax
    12ad: 7d 55                        	jge	0x1304 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11a4>
    12af: f2 42 0f 11 04 ef            	movsd	%xmm0, (%rdi,%r13,8)
    12b5: 49 ff c5                     	incq	%r13
    12b8: 49 39 ed                     	cmpq	%rbp, %r13
    12bb: 0f 85 ff fd ff ff            	jne	0x10c0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0xf60>
    12c1: e9 ea f1 ff ff               	jmp	0x4b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x350>
    12c6: b3 0d                        	movb	$0xd, %bl
    12c8: 31 ff                        	xorl	%edi, %edi
    12ca: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    12cf: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
    12d4: 4c 8d bc 24 90 00 00 00      	leaq	0x90(%rsp), %r15
    12dc: eb 4f                        	jmp	0x132d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11cd>
    12de: 31 ff                        	xorl	%edi, %edi
    12e0: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    12e5: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
    12ea: 4c 8d bc 24 90 00 00 00      	leaq	0x90(%rsp), %r15
    12f2: eb 37                        	jmp	0x132b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11cb>
    12f4: 31 ff                        	xorl	%edi, %edi
    12f6: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    12fb: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
    1300: b3 0d                        	movb	$0xd, %bl
    1302: eb 29                        	jmp	0x132d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11cd>
    1304: 31 ff                        	xorl	%edi, %edi
    1306: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    130b: eb 1e                        	jmp	0x132b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11cb>
    130d: 48 8b bc 24 a0 00 00 00      	movq	0xa0(%rsp), %rdi
    1315: 89 c3                        	movl	%eax, %ebx
    1317: eb 3f                        	jmp	0x1358 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11f8>
    1319: 48 8b bc 24 a0 00 00 00      	movq	0xa0(%rsp), %rdi
    1321: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    1326: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
    132b: b3 0b                        	movb	$0xb, %bl
    132d: b9 10 00 00 00               	movl	$0x10, %ecx
    1332: e8 00 00 00 00               	callq	0x1337 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11d7>
    1337: 88 18                        	movb	%bl, (%rax)
    1339: 48 89 78 08                  	movq	%rdi, 0x8(%rax)
    133d: 48 89 c1                     	movq	%rax, %rcx
    1340: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x1347 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11e7>
    1347: 45 31 c0                     	xorl	%r8d, %r8d
    134a: e8 00 00 00 00               	callq	0x134f <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11ef>
    134f: e9 f4 09 00 00               	jmp	0x1d48 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1be8>
    1354: b3 0d                        	movb	$0xd, %bl
    1356: 31 ff                        	xorl	%edi, %edi
    1358: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    135d: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
    1362: eb c9                        	jmp	0x132d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11cd>
    1364: b3 0c                        	movb	$0xc, %bl
    1366: 48 8b bc 24 a0 00 00 00      	movq	0xa0(%rsp), %rdi
    136e: eb bd                        	jmp	0x132d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11cd>
    1370: b3 0c                        	movb	$0xc, %bl
    1372: eb 06                        	jmp	0x137a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x121a>
    1374: 31 ff                        	xorl	%edi, %edi
    1376: eb b3                        	jmp	0x132b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11cb>
    1378: b3 0b                        	movb	$0xb, %bl
    137a: 48 8b bc 24 a0 00 00 00      	movq	0xa0(%rsp), %rdi
    1382: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    1387: eb a4                        	jmp	0x132d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11cd>
    1389: 31 ff                        	xorl	%edi, %edi
    138b: eb 94                        	jmp	0x1321 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11c1>
    138d: 31 ff                        	xorl	%edi, %edi
    138f: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
    1394: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    1399: b3 0d                        	movb	$0xd, %bl
    139b: eb 90                        	jmp	0x132d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x11cd>
    139d: 83 fa 01                     	cmpl	$0x1, %edx
    13a0: 0f 85 8d 13 00 00            	jne	0x2733 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25d3>
    13a6: 48 89 c1                     	movq	%rax, %rcx
    13a9: e8 00 00 00 00               	callq	0x13ae <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x124e>
    13ae: 80 38 0c                     	cmpb	$0xc, (%rax)
    13b1: 0f 85 87 09 00 00            	jne	0x1d3e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1bde>
    13b7: e8 00 00 00 00               	callq	0x13bc <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x125c>
    13bc: 31 c0                        	xorl	%eax, %eax
    13be: e9 fb f0 ff ff               	jmp	0x4be <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x35e>
    13c3: 48 8b 84 24 b0 00 00 00      	movq	0xb0(%rsp), %rax
    13cb: 48 8b b8 80 04 00 00         	movq	0x480(%rax), %rdi
    13d2: 48 8b 98 88 04 00 00         	movq	0x488(%rax), %rbx
    13d9: eb 19                        	jmp	0x13f4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1294>
    13db: 0f 1f 44 00 00               	nopl	(%rax,%rax)
    13e0: c6 84 06 ff 01 00 00 01      	movb	$0x1, 0x1ff(%rsi,%rax)
    13e8: c6 84 0e ff 01 00 00 01      	movb	$0x1, 0x1ff(%rsi,%rcx)
    13f0: 48 83 c7 08                  	addq	$0x8, %rdi
    13f4: 48 39 df                     	cmpq	%rbx, %rdi
    13f7: 74 1b                        	je	0x1414 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x12b4>
    13f9: 8b 07                        	movl	(%rdi), %eax
    13fb: 8b 4f 04                     	movl	0x4(%rdi), %ecx
    13fe: 80 bc 06 ff 01 00 00 00      	cmpb	$0x0, 0x1ff(%rsi,%rax)
    1406: 75 d8                        	jne	0x13e0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1280>
    1408: 80 bc 0e ff 01 00 00 01      	cmpb	$0x1, 0x1ff(%rsi,%rcx)
    1410: 74 ce                        	je	0x13e0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1280>
    1412: eb dc                        	jmp	0x13f0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1290>
    1414: 48 8b 84 24 b0 00 00 00      	movq	0xb0(%rsp), %rax
    141c: 83 38 00                     	cmpl	$0x0, (%rax)
    141f: 0f 84 4f 09 00 00            	je	0x1d74 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c14>
    1425: 45 31 c0                     	xorl	%r8d, %r8d
    1428: eb 1c                        	jmp	0x1446 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x12e6>
    142a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
    1430: 49 ff c0                     	incq	%r8
    1433: 48 8b 84 24 b0 00 00 00      	movq	0xb0(%rsp), %rax
    143b: 8b 00                        	movl	(%rax), %eax
    143d: 49 39 c0                     	cmpq	%rax, %r8
    1440: 0f 83 2e 09 00 00            	jae	0x1d74 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c14>
    1446: 45 85 ed                     	testl	%r13d, %r13d
    1449: 74 e5                        	je	0x1430 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x12d0>
    144b: 48 8b 84 24 b0 00 00 00      	movq	0xb0(%rsp), %rax
    1453: 42 8b 84 80 40 04 00 00      	movl	0x440(%rax,%r8,4), %eax
    145b: 4c 69 f0 18 08 00 00         	imulq	$0x818, %rax, %r14      # imm = 0x818
    1462: 4d 03 74 24 50               	addq	0x50(%r12), %r14
    1467: 31 ff                        	xorl	%edi, %edi
    1469: 31 c9                        	xorl	%ecx, %ecx
    146b: 31 c0                        	xorl	%eax, %eax
    146d: eb 16                        	jmp	0x1485 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1325>
    146f: 90                           	nop
    1470: 08 d0                        	orb	%dl, %al
    1472: 89 fa                        	movl	%edi, %edx
    1474: ff c7                        	incl	%edi
    1476: 89 8c 96 00 03 00 00         	movl	%ecx, 0x300(%rsi,%rdx,4)
    147d: 48 ff c1                     	incq	%rcx
    1480: 49 39 cd                     	cmpq	%rcx, %r13
    1483: 74 2b                        	je	0x14b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1350>
    1485: 48 8b 94 24 b0 00 00 00      	movq	0xb0(%rsp), %rdx
    148d: 8b 54 8a 04                  	movl	0x4(%rdx,%rcx,4), %edx
    1491: 49 39 d0                     	cmpq	%rdx, %r8
    1494: 75 e7                        	jne	0x147d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x131d>
    1496: 0f b6 94 0e ff 01 00 00      	movzbl	0x1ff(%rsi,%rcx), %edx
    149e: 41 83 3e 02                  	cmpl	$0x2, (%r14)
    14a2: 74 cc                        	je	0x1470 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1310>
    14a4: 84 d2                        	testb	%dl, %dl
    14a6: 74 d5                        	je	0x147d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x131d>
    14a8: eb c6                        	jmp	0x1470 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1310>
    14aa: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
    14b0: 85 ff                        	testl	%edi, %edi
    14b2: 0f 95 c1                     	setne	%cl
    14b5: 84 c8                        	testb	%cl, %al
    14b7: 0f 84 73 ff ff ff            	je	0x1430 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x12d0>
    14bd: 41 83 3e 02                  	cmpl	$0x2, (%r14)
    14c1: 89 f9                        	movl	%edi, %ecx
    14c3: b8 01 00 00 00               	movl	$0x1, %eax
    14c8: 0f 44 c8                     	cmovel	%eax, %ecx
    14cb: 89 8c 24 f0 00 00 00         	movl	%ecx, 0xf0(%rsp)
    14d2: 41 8b 56 04                  	movl	0x4(%r14), %edx
    14d6: b9 01 00 00 00               	movl	$0x1, %ecx
    14db: 0f 44 cf                     	cmovel	%edi, %ecx
    14de: 0f af cd                     	imull	%ebp, %ecx
    14e1: 39 d1                        	cmpl	%edx, %ecx
    14e3: 0f 42 d1                     	cmovbl	%ecx, %edx
    14e6: 41 8b 46 08                  	movl	0x8(%r14), %eax
    14ea: 39 c1                        	cmpl	%eax, %ecx
    14ec: 0f 43 c8                     	cmovael	%eax, %ecx
    14ef: 89 94 24 30 01 00 00         	movl	%edx, 0x130(%rsp)
    14f6: 29 d1                        	subl	%edx, %ecx
    14f8: 4c 89 84 24 40 01 00 00      	movq	%r8, 0x140(%rsp)
    1500: 45 8b 46 0c                  	movl	0xc(%r14), %r8d
    1504: 89 c8                        	movl	%ecx, %eax
    1506: 31 d2                        	xorl	%edx, %edx
    1508: 41 f7 f0                     	divl	%r8d
    150b: 89 44 24 58                  	movl	%eax, 0x58(%rsp)
    150f: 41 39 c8                     	cmpl	%ecx, %r8d
    1512: 4c 8b 84 24 40 01 00 00      	movq	0x140(%rsp), %r8
    151a: 0f 87 10 ff ff ff            	ja	0x1430 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x12d0>
    1520: 8b 44 24 58                  	movl	0x58(%rsp), %eax
    1524: 8b 94 24 f0 00 00 00         	movl	0xf0(%rsp), %edx
    152b: 48 8b 8e e8 00 00 00         	movq	0xe8(%rsi), %rcx
    1532: 48 2b 8e e0 00 00 00         	subq	0xe0(%rsi), %rcx
    1539: 48 89 94 24 e0 00 00 00      	movq	%rdx, 0xe0(%rsp)
    1541: 48 0f af c2                  	imulq	%rdx, %rax
    1545: 48 c1 f9 02                  	sarq	$0x2, %rcx
    1549: 48 39 c8                     	cmpq	%rcx, %rax
    154c: 0f 87 57 07 00 00            	ja	0x1ca9 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b49>
    1552: 41 8b 46 14                  	movl	0x14(%r14), %eax
    1556: 4c 69 f8 50 20 00 00         	imulq	$0x2050, %rax, %r15     # imm = 0x2050
    155d: 4d 03 7c 24 10               	addq	0x10(%r12), %r15
    1562: 49 8d 46 18                  	leaq	0x18(%r14), %rax
    1566: 31 d2                        	xorl	%edx, %edx
    1568: 4c 89 bc 24 c8 00 00 00      	movq	%r15, 0xc8(%rsp)
    1570: 48 89 84 24 20 01 00 00      	movq	%rax, 0x120(%rsp)
    1578: 4c 89 74 24 38               	movq	%r14, 0x38(%rsp)
    157d: eb 1b                        	jmp	0x159a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x143a>
    157f: 90                           	nop
    1580: 48 ff c2                     	incq	%rdx
    1583: 48 83 fa 08                  	cmpq	$0x8, %rdx
    1587: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
    158c: 4c 8b 84 24 40 01 00 00      	movq	0x140(%rsp), %r8
    1594: 0f 84 96 fe ff ff            	je	0x1430 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x12d0>
    159a: 48 8d 04 90                  	leaq	(%rax,%rdx,4), %rax
    159e: 48 89 44 24 78               	movq	%rax, 0x78(%rsp)
    15a3: 45 31 c0                     	xorl	%r8d, %r8d
    15a6: 48 89 94 24 28 01 00 00      	movq	%rdx, 0x128(%rsp)
    15ae: eb 2c                        	jmp	0x15dc <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x147c>
    15b0: 44 3b 44 24 58               	cmpl	0x58(%rsp), %r8d
    15b5: 4c 8b a4 24 d8 00 00 00      	movq	0xd8(%rsp), %r12
    15bd: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    15c2: 4c 8b bc 24 c8 00 00 00      	movq	0xc8(%rsp), %r15
    15ca: 48 8b 84 24 20 01 00 00      	movq	0x120(%rsp), %rax
    15d2: 48 8b 94 24 28 01 00 00      	movq	0x128(%rsp), %rdx
    15da: 73 a4                        	jae	0x1580 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1420>
    15dc: 48 85 d2                     	testq	%rdx, %rdx
    15df: 74 0f                        	je	0x15f0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1490>
    15e1: 4d 89 c5                     	movq	%r8, %r13
    15e4: 45 8b 1f                     	movl	(%r15), %r11d
    15e7: e9 0c 01 00 00               	jmp	0x16f8 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1598>
    15ec: 0f 1f 40 00                  	nopl	(%rax)
    15f0: 4d 89 c5                     	movq	%r8, %r13
    15f3: 41 8d 58 ff                  	leal	-0x1(%r8), %ebx
    15f7: 48 8b 96 e8 13 00 00         	movq	0x13e8(%rsi), %rdx
    15fe: 4c 8b 86 f0 13 00 00         	movq	0x13f0(%rsi), %r8
    1605: 31 ed                        	xorl	%ebp, %ebp
    1607: 44 8b a4 24 f0 00 00 00      	movl	0xf0(%rsp), %r12d
    160f: 90                           	nop
    1610: 49 39 d0                     	cmpq	%rdx, %r8
    1613: 0f 84 cc 06 00 00            	je	0x1ce5 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b85>
    1619: 48 ff c2                     	incq	%rdx
    161c: 48 89 96 e8 13 00 00         	movq	%rdx, 0x13e8(%rsi)
    1623: c7 44 24 54 00 00 00 00      	movl	$0x0, 0x54(%rsp)
    162b: 4c 89 f9                     	movq	%r15, %rcx
    162e: 48 8d 94 24 90 00 00 00      	leaq	0x90(%rsp), %rdx
    1636: 4c 8d 44 24 54               	leaq	0x54(%rsp), %r8
    163b: e8 00 00 00 00               	callq	0x1640 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x14e0>
    1640: 84 c0                        	testb	%al, %al
    1642: 0f 85 67 06 00 00            	jne	0x1caf <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b4f>
    1648: 8b 44 24 54                  	movl	0x54(%rsp), %eax
    164c: 41 8b 0f                     	movl	(%r15), %ecx
    164f: 48 85 c9                     	testq	%rcx, %rcx
    1652: 74 5c                        	je	0x16b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1550>
    1654: 41 89 e8                     	movl	%ebp, %r8d
    1657: 44 0f af 44 24 58            	imull	0x58(%rsp), %r8d
    165d: 4c 8b 8e e0 00 00 00         	movq	0xe0(%rsi), %r9
    1664: eb 11                        	jmp	0x1677 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1517>
    1666: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
    1670: 48 ff c9                     	decq	%rcx
    1673: 85 c9                        	testl	%ecx, %ecx
    1675: 74 29                        	je	0x16a0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1540>
    1677: 31 d2                        	xorl	%edx, %edx
    1679: 41 f7 76 10                  	divl	0x10(%r14)
    167d: 44 8d 14 0b                  	leal	(%rbx,%rcx), %r10d
    1681: 44 3b 54 24 58               	cmpl	0x58(%rsp), %r10d
    1686: 73 e8                        	jae	0x1670 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1510>
    1688: 45 01 c2                     	addl	%r8d, %r10d
    168b: 43 89 14 91                  	movl	%edx, (%r9,%r10,4)
    168f: eb df                        	jmp	0x1670 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1510>
    1691: 66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00 	nopw	%cs:(%rax,%rax)
    16a0: 48 8b 8c 24 c8 00 00 00      	movq	0xc8(%rsp), %rcx
    16a8: 44 8b 19                     	movl	(%rcx), %r11d
    16ab: eb 06                        	jmp	0x16b3 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1553>
    16ad: 0f 1f 00                     	nopl	(%rax)
    16b0: 45 31 db                     	xorl	%r11d, %r11d
    16b3: 45 89 d9                     	movl	%r11d, %r9d
    16b6: 48 8b 96 e8 13 00 00         	movq	0x13e8(%rsi), %rdx
    16bd: 4c 8b 86 f0 13 00 00         	movq	0x13f0(%rsi), %r8
    16c4: 4d 89 c2                     	movq	%r8, %r10
    16c7: 49 29 d2                     	subq	%rdx, %r10
    16ca: 4d 39 ca                     	cmpq	%r9, %r10
    16cd: 0f 82 12 06 00 00            	jb	0x1ce5 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b85>
    16d3: 4c 01 ca                     	addq	%r9, %rdx
    16d6: 48 89 96 e8 13 00 00         	movq	%rdx, 0x13e8(%rsi)
    16dd: 85 c0                        	testl	%eax, %eax
    16df: 0f 85 0b 06 00 00            	jne	0x1cf0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b90>
    16e5: ff c5                        	incl	%ebp
    16e7: 44 39 e5                     	cmpl	%r12d, %ebp
    16ea: 4c 8b bc 24 c8 00 00 00      	movq	0xc8(%rsp), %r15
    16f2: 0f 85 18 ff ff ff            	jne	0x1610 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x14b0>
    16f8: 8b 44 24 58                  	movl	0x58(%rsp), %eax
    16fc: 4d 89 e8                     	movq	%r13, %r8
    16ff: 44 29 c0                     	subl	%r8d, %eax
    1702: 44 39 d8                     	cmpl	%r11d, %eax
    1705: 41 0f 43 c3                  	cmovael	%r11d, %eax
    1709: 89 84 24 d4 00 00 00         	movl	%eax, 0xd4(%rsp)
    1710: 85 c0                        	testl	%eax, %eax
    1712: 0f 84 98 fe ff ff            	je	0x15b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1450>
    1718: 31 c0                        	xorl	%eax, %eax
    171a: eb 22                        	jmp	0x173e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x15de>
    171c: 0f 1f 40 00                  	nopl	(%rax)
    1720: 8b 84 24 38 01 00 00         	movl	0x138(%rsp), %eax
    1727: ff c0                        	incl	%eax
    1729: 41 ff c0                     	incl	%r8d
    172c: 3b 84 24 d4 00 00 00         	cmpl	0xd4(%rsp), %eax
    1733: 4c 8b 74 24 38               	movq	0x38(%rsp), %r14
    1738: 0f 83 72 fe ff ff            	jae	0x15b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1450>
    173e: 89 84 24 38 01 00 00         	movl	%eax, 0x138(%rsp)
    1745: 41 8b 46 0c                  	movl	0xc(%r14), %eax
    1749: 41 0f af c0                  	imull	%r8d, %eax
    174d: 03 84 24 30 01 00 00         	addl	0x130(%rsp), %eax
    1754: 48 89 44 24 48               	movq	%rax, 0x48(%rsp)
    1759: 48 8d 04 c5 00 00 00 00      	leaq	(,%rax,8), %rax
    1761: 48 89 84 24 b8 00 00 00      	movq	%rax, 0xb8(%rsp)
    1769: 31 c9                        	xorl	%ecx, %ecx
    176b: 4c 89 84 24 a8 00 00 00      	movq	%r8, 0xa8(%rsp)
    1773: eb 25                        	jmp	0x179a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x163a>
    1775: 66 66 2e 0f 1f 84 00 00 00 00 00     	nopw	%cs:(%rax,%rax)
    1780: 48 8b 4c 24 68               	movq	0x68(%rsp), %rcx
    1785: 48 ff c1                     	incq	%rcx
    1788: 48 3b 8c 24 e0 00 00 00      	cmpq	0xe0(%rsp), %rcx
    1790: 4c 8b 84 24 a8 00 00 00      	movq	0xa8(%rsp), %r8
    1798: 74 86                        	je	0x1720 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x15c0>
    179a: 8b 44 24 58                  	movl	0x58(%rsp), %eax
    179e: 48 89 4c 24 68               	movq	%rcx, 0x68(%rsp)
    17a3: 0f af c1                     	imull	%ecx, %eax
    17a6: 44 01 c0                     	addl	%r8d, %eax
    17a9: 48 8b 8e e0 00 00 00         	movq	0xe0(%rsi), %rcx
    17b0: 8b 04 81                     	movl	(%rcx,%rax,4), %eax
    17b3: 48 c1 e0 05                  	shlq	$0x5, %rax
    17b7: 48 8b 4c 24 78               	movq	0x78(%rsp), %rcx
    17bc: 48 63 04 01                  	movslq	(%rcx,%rax), %rax
    17c0: 48 85 c0                     	testq	%rax, %rax
    17c3: 78 bb                        	js	0x1780 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1620>
    17c5: 4c 69 e0 50 20 00 00         	imulq	$0x2050, %rax, %r12     # imm = 0x2050
    17cc: 48 8b 54 24 38               	movq	0x38(%rsp), %rdx
    17d1: 8b 4a 0c                     	movl	0xc(%rdx), %ecx
    17d4: 48 8b 84 24 d8 00 00 00      	movq	0xd8(%rsp), %rax
    17dc: 4c 03 60 10                  	addq	0x10(%rax), %r12
    17e0: 83 3a 00                     	cmpl	$0x0, (%rdx)
    17e3: 0f 84 47 03 00 00            	je	0x1b30 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x19d0>
    17e9: 85 c9                        	testl	%ecx, %ecx
    17eb: 74 93                        	je	0x1780 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1620>
    17ed: 48 8b 8e e8 13 00 00         	movq	0x13e8(%rsi), %rcx
    17f4: 4c 8b 86 f0 13 00 00         	movq	0x13f0(%rsi), %r8
    17fb: 45 31 ed                     	xorl	%r13d, %r13d
    17fe: eb 0f                        	jmp	0x180f <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x16af>
    1800: 45 01 ea                     	addl	%r13d, %r10d
    1803: 45 89 d5                     	movl	%r10d, %r13d
    1806: 45 39 ca                     	cmpl	%r9d, %r10d
    1809: 0f 83 71 ff ff ff            	jae	0x1780 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1620>
    180f: 49 39 c8                     	cmpq	%rcx, %r8
    1812: 0f 84 8c 04 00 00            	je	0x1ca4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b44>
    1818: 48 ff c1                     	incq	%rcx
    181b: 48 89 8e e8 13 00 00         	movq	%rcx, 0x13e8(%rsi)
    1822: c7 44 24 54 00 00 00 00      	movl	$0x0, 0x54(%rsp)
    182a: 4c 89 e1                     	movq	%r12, %rcx
    182d: 48 8d 94 24 90 00 00 00      	leaq	0x90(%rsp), %rdx
    1835: 4c 8d 44 24 54               	leaq	0x54(%rsp), %r8
    183a: e8 00 00 00 00               	callq	0x183f <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x16df>
    183f: 84 c0                        	testb	%al, %al
    1841: 0f 85 68 04 00 00            	jne	0x1caf <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b4f>
    1847: 48 8b 54 24 38               	movq	0x38(%rsp), %rdx
    184c: 44 8b 4a 0c                  	movl	0xc(%rdx), %r9d
    1850: 45 89 ca                     	movl	%r9d, %r10d
    1853: 45 29 ea                     	subl	%r13d, %r10d
    1856: 45 8b 1c 24                  	movl	(%r12), %r11d
    185a: 45 39 da                     	cmpl	%r11d, %r10d
    185d: 45 0f 43 d3                  	cmovael	%r11d, %r10d
    1861: 48 8b 8e e8 13 00 00         	movq	0x13e8(%rsi), %rcx
    1868: 4c 8b 86 f0 13 00 00         	movq	0x13f0(%rsi), %r8
    186f: 4c 89 c0                     	movq	%r8, %rax
    1872: 48 29 c8                     	subq	%rcx, %rax
    1875: 4c 39 d0                     	cmpq	%r10, %rax
    1878: 0f 82 26 04 00 00            	jb	0x1ca4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b44>
    187e: 8b 44 24 54                  	movl	0x54(%rsp), %eax
    1882: 4c 01 d1                     	addq	%r10, %rcx
    1885: 48 89 8e e8 13 00 00         	movq	%rcx, 0x13e8(%rsi)
    188c: 4c 0f af d8                  	imulq	%rax, %r11
    1890: 83 3a 01                     	cmpl	$0x1, (%rdx)
    1893: 0f 85 b7 01 00 00            	jne	0x1a50 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x18f0>
    1899: 4d 85 d2                     	testq	%r10, %r10
    189c: 0f 84 5e ff ff ff            	je	0x1800 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x16a0>
    18a2: 45 89 ef                     	movl	%r13d, %r15d
    18a5: 8b 86 dc 13 00 00            	movl	0x13dc(%rsi), %eax
    18ab: 48 8b 54 24 68               	movq	0x68(%rsp), %rdx
    18b0: 44 8b b4 96 00 03 00 00      	movl	0x300(%rsi,%rdx,4), %r14d
    18b8: 4c 0f af f0                  	imulq	%rax, %r14
    18bc: 49 8b 84 24 30 20 00 00      	movq	0x2030(%r12), %rax
    18c4: 48 8b 2e                     	movq	(%rsi), %rbp
    18c7: 41 83 fa 0a                  	cmpl	$0xa, %r10d
    18cb: 4c 89 bc 24 80 00 00 00      	movq	%r15, 0x80(%rsp)
    18d3: 48 89 84 24 88 00 00 00      	movq	%rax, 0x88(%rsp)
    18db: 72 49                        	jb	0x1926 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x17c6>
    18dd: 4a 8d 04 d8                  	leaq	(%rax,%r11,8), %rax
    18e1: 4a 8d 14 f5 00 00 00 00      	leaq	(,%r14,8), %rdx
    18e9: 48 01 ea                     	addq	%rbp, %rdx
    18ec: 48 8b 74 24 48               	movq	0x48(%rsp), %rsi
    18f1: 48 8d 14 f2                  	leaq	(%rdx,%rsi,8), %rdx
    18f5: 4a 8d 34 fa                  	leaq	(%rdx,%r15,8), %rsi
    18f9: 48 8b 94 24 b8 00 00 00      	movq	0xb8(%rsp), %rdx
    1901: 48 01 ea                     	addq	%rbp, %rdx
    1904: 4a 8d 1c d0                  	leaq	(%rax,%r10,8), %rbx
    1908: 48 39 de                     	cmpq	%rbx, %rsi
    190b: 0f 83 a2 01 00 00            	jae	0x1ab3 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1953>
    1911: 4a 8d 34 f2                  	leaq	(%rdx,%r14,8), %rsi
    1915: 4a 8d 34 d6                  	leaq	(%rsi,%r10,8), %rsi
    1919: 4a 8d 34 fe                  	leaq	(%rsi,%r15,8), %rsi
    191d: 48 39 f0                     	cmpq	%rsi, %rax
    1920: 0f 83 8d 01 00 00            	jae	0x1ab3 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1953>
    1926: 31 db                        	xorl	%ebx, %ebx
    1928: 4c 89 d6                     	movq	%r10, %rsi
    192b: 49 89 df                     	movq	%rbx, %r15
    192e: 48 83 e6 03                  	andq	$0x3, %rsi
    1932: 74 69                        	je	0x199d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x183d>
    1934: 48 8b 84 24 b8 00 00 00      	movq	0xb8(%rsp), %rax
    193c: 48 01 e8                     	addq	%rbp, %rax
    193f: 48 89 84 24 c0 00 00 00      	movq	%rax, 0xc0(%rsp)
    1947: 4c 8d 3c dd 00 00 00 00      	leaq	(,%rbx,8), %r15
    194f: 4b 8d 14 f7                  	leaq	(%r15,%r14,8), %rdx
    1953: 48 8b 84 24 80 00 00 00      	movq	0x80(%rsp), %rax
    195b: 48 8d 14 c2                  	leaq	(%rdx,%rax,8), %rdx
    195f: 48 03 94 24 c0 00 00 00      	addq	0xc0(%rsp), %rdx
    1967: 4b 8d 04 df                  	leaq	(%r15,%r11,8), %rax
    196b: 48 03 84 24 88 00 00 00      	addq	0x88(%rsp), %rax
    1973: 45 31 ff                     	xorl	%r15d, %r15d
    1976: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
    1980: f2 42 0f 10 04 f8            	movsd	(%rax,%r15,8), %xmm0
    1986: f2 42 0f 58 04 fa            	addsd	(%rdx,%r15,8), %xmm0
    198c: f2 42 0f 11 04 fa            	movsd	%xmm0, (%rdx,%r15,8)
    1992: 49 ff c7                     	incq	%r15
    1995: 4c 39 fe                     	cmpq	%r15, %rsi
    1998: 75 e6                        	jne	0x1980 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1820>
    199a: 49 01 df                     	addq	%rbx, %r15
    199d: 4c 29 d3                     	subq	%r10, %rbx
    19a0: 48 83 fb fc                  	cmpq	$-0x4, %rbx
    19a4: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
    19a9: 0f 87 51 fe ff ff            	ja	0x1800 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x16a0>
    19af: 4c 89 d3                     	movq	%r10, %rbx
    19b2: 4c 29 fb                     	subq	%r15, %rbx
    19b5: 48 03 ac 24 b8 00 00 00      	addq	0xb8(%rsp), %rbp
    19bd: 49 c1 e7 03                  	shlq	$0x3, %r15
    19c1: 4b 8d 04 f7                  	leaq	(%r15,%r14,8), %rax
    19c5: 48 8b 94 24 80 00 00 00      	movq	0x80(%rsp), %rdx
    19cd: 48 8d 04 d0                  	leaq	(%rax,%rdx,8), %rax
    19d1: 48 01 e8                     	addq	%rbp, %rax
    19d4: 48 83 c0 18                  	addq	$0x18, %rax
    19d8: 4b 8d 14 df                  	leaq	(%r15,%r11,8), %rdx
    19dc: 4c 8b 9c 24 88 00 00 00      	movq	0x88(%rsp), %r11
    19e4: 4c 01 da                     	addq	%r11, %rdx
    19e7: 48 83 c2 18                  	addq	$0x18, %rdx
    19eb: 45 31 db                     	xorl	%r11d, %r11d
    19ee: 66 90                        	nop
    19f0: f2 42 0f 10 44 da e8         	movsd	-0x18(%rdx,%r11,8), %xmm0
    19f7: f2 42 0f 58 44 d8 e8         	addsd	-0x18(%rax,%r11,8), %xmm0
    19fe: f2 42 0f 11 44 d8 e8         	movsd	%xmm0, -0x18(%rax,%r11,8)
    1a05: f2 42 0f 10 44 da f0         	movsd	-0x10(%rdx,%r11,8), %xmm0
    1a0c: f2 42 0f 58 44 d8 f0         	addsd	-0x10(%rax,%r11,8), %xmm0
    1a13: f2 42 0f 11 44 d8 f0         	movsd	%xmm0, -0x10(%rax,%r11,8)
    1a1a: f2 42 0f 10 44 da f8         	movsd	-0x8(%rdx,%r11,8), %xmm0
    1a21: f2 42 0f 58 44 d8 f8         	addsd	-0x8(%rax,%r11,8), %xmm0
    1a28: f2 42 0f 11 44 d8 f8         	movsd	%xmm0, -0x8(%rax,%r11,8)
    1a2f: f2 42 0f 10 04 da            	movsd	(%rdx,%r11,8), %xmm0
    1a35: f2 42 0f 58 04 d8            	addsd	(%rax,%r11,8), %xmm0
    1a3b: f2 42 0f 11 04 d8            	movsd	%xmm0, (%rax,%r11,8)
    1a41: 49 83 c3 04                  	addq	$0x4, %r11
    1a45: 4c 39 db                     	cmpq	%r11, %rbx
    1a48: 75 a6                        	jne	0x19f0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1890>
    1a4a: e9 b1 fd ff ff               	jmp	0x1800 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x16a0>
    1a4f: 90                           	nop
    1a50: 4d 85 d2                     	testq	%r10, %r10
    1a53: 0f 84 a7 fd ff ff            	je	0x1800 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x16a0>
    1a59: 49 c1 e3 03                  	shlq	$0x3, %r11
    1a5d: 4d 03 9c 24 30 20 00 00      	addq	0x2030(%r12), %r11
    1a65: 44 8b b6 dc 13 00 00         	movl	0x13dc(%rsi), %r14d
    1a6c: 4c 8b 3e                     	movq	(%rsi), %r15
    1a6f: 48 8b 44 24 48               	movq	0x48(%rsp), %rax
    1a74: 42 8d 1c 28                  	leal	(%rax,%r13), %ebx
    1a78: 31 ed                        	xorl	%ebp, %ebp
    1a7a: 66 0f 1f 44 00 00            	nopw	(%rax,%rax)
    1a80: 8d 04 2b                     	leal	(%rbx,%rbp), %eax
    1a83: 31 d2                        	xorl	%edx, %edx
    1a85: f7 f7                        	divl	%edi
    1a87: 8b 94 96 00 03 00 00         	movl	0x300(%rsi,%rdx,4), %edx
    1a8e: 49 0f af d6                  	imulq	%r14, %rdx
    1a92: f2 41 0f 10 04 eb            	movsd	(%r11,%rbp,8), %xmm0
    1a98: 49 8d 14 d7                  	leaq	(%r15,%rdx,8), %rdx
    1a9c: f2 0f 58 04 c2               	addsd	(%rdx,%rax,8), %xmm0
    1aa1: f2 0f 11 04 c2               	movsd	%xmm0, (%rdx,%rax,8)
    1aa6: 48 ff c5                     	incq	%rbp
    1aa9: 49 39 ea                     	cmpq	%rbp, %r10
    1aac: 75 d2                        	jne	0x1a80 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1920>
    1aae: e9 4d fd ff ff               	jmp	0x1800 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x16a0>
    1ab3: 44 89 d3                     	movl	%r10d, %ebx
    1ab6: 83 e3 fc                     	andl	$-0x4, %ebx
    1ab9: 4a 8d 04 fd 00 00 00 00      	leaq	(,%r15,8), %rax
    1ac1: 4a 8d 04 f0                  	leaq	(%rax,%r14,8), %rax
    1ac5: 48 8d 34 02                  	leaq	(%rdx,%rax), %rsi
    1ac9: 48 83 c6 10                  	addq	$0x10, %rsi
    1acd: 48 8b 84 24 88 00 00 00      	movq	0x88(%rsp), %rax
    1ad5: 4e 8d 3c d8                  	leaq	(%rax,%r11,8), %r15
    1ad9: 49 83 c7 10                  	addq	$0x10, %r15
    1add: 31 d2                        	xorl	%edx, %edx
    1adf: 90                           	nop
    1ae0: 66 41 0f 10 44 d7 f0         	movupd	-0x10(%r15,%rdx,8), %xmm0
    1ae7: 66 41 0f 10 0c d7            	movupd	(%r15,%rdx,8), %xmm1
    1aed: 66 0f 10 54 d6 f0            	movupd	-0x10(%rsi,%rdx,8), %xmm2
    1af3: 66 0f 58 d0                  	addpd	%xmm0, %xmm2
    1af7: 66 0f 10 04 d6               	movupd	(%rsi,%rdx,8), %xmm0
    1afc: 66 0f 58 c1                  	addpd	%xmm1, %xmm0
    1b00: 66 0f 11 54 d6 f0            	movupd	%xmm2, -0x10(%rsi,%rdx,8)
    1b06: 66 0f 11 04 d6               	movupd	%xmm0, (%rsi,%rdx,8)
    1b0b: 48 83 c2 04                  	addq	$0x4, %rdx
    1b0f: 48 39 d3                     	cmpq	%rdx, %rbx
    1b12: 75 cc                        	jne	0x1ae0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1980>
    1b14: 44 39 d3                     	cmpl	%r10d, %ebx
    1b17: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
    1b1c: 0f 84 de fc ff ff            	je	0x1800 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x16a0>
    1b22: e9 01 fe ff ff               	jmp	0x1928 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x17c8>
    1b27: 66 0f 1f 84 00 00 00 00 00   	nopw	(%rax,%rax)
    1b30: 45 8b 04 24                  	movl	(%r12), %r8d
    1b34: 89 c8                        	movl	%ecx, %eax
    1b36: 31 d2                        	xorl	%edx, %edx
    1b38: 41 f7 f0                     	divl	%r8d
    1b3b: 41 39 c8                     	cmpl	%ecx, %r8d
    1b3e: 0f 87 3c fc ff ff            	ja	0x1780 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1620>
    1b44: 41 89 c5                     	movl	%eax, %r13d
    1b47: 41 89 c6                     	movl	%eax, %r14d
    1b4a: 48 8b 86 e8 13 00 00         	movq	0x13e8(%rsi), %rax
    1b51: 48 8b 8e f0 13 00 00         	movq	0x13f0(%rsi), %rcx
    1b58: 31 ed                        	xorl	%ebp, %ebp
    1b5a: eb 2d                        	jmp	0x1b89 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1a29>
    1b5c: 0f 1f 40 00                  	nopl	(%rax)
    1b60: 45 31 db                     	xorl	%r11d, %r11d
    1b63: 49 8d 14 d2                  	leaq	(%r10,%rdx,8), %rdx
    1b67: f2 42 0f 10 04 da            	movsd	(%rdx,%r11,8), %xmm0
    1b6d: 45 0f af dd                  	imull	%r13d, %r11d
    1b71: f2 43 0f 58 04 d9            	addsd	(%r9,%r11,8), %xmm0
    1b77: f2 43 0f 11 04 d9            	movsd	%xmm0, (%r9,%r11,8)
    1b7d: 48 ff c5                     	incq	%rbp
    1b80: 4c 39 f5                     	cmpq	%r14, %rbp
    1b83: 0f 83 f7 fb ff ff            	jae	0x1780 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1620>
    1b89: 48 39 c1                     	cmpq	%rax, %rcx
    1b8c: 0f 84 12 01 00 00            	je	0x1ca4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b44>
    1b92: 48 ff c0                     	incq	%rax
    1b95: 48 89 86 e8 13 00 00         	movq	%rax, 0x13e8(%rsi)
    1b9c: c7 44 24 54 00 00 00 00      	movl	$0x0, 0x54(%rsp)
    1ba4: 4c 89 e1                     	movq	%r12, %rcx
    1ba7: 48 8d 94 24 90 00 00 00      	leaq	0x90(%rsp), %rdx
    1baf: 4c 8d 44 24 54               	leaq	0x54(%rsp), %r8
    1bb4: e8 00 00 00 00               	callq	0x1bb9 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1a59>
    1bb9: 84 c0                        	testb	%al, %al
    1bbb: 0f 85 ee 00 00 00            	jne	0x1caf <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b4f>
    1bc1: 45 8b 04 24                  	movl	(%r12), %r8d
    1bc5: 48 8b 86 e8 13 00 00         	movq	0x13e8(%rsi), %rax
    1bcc: 48 8b 8e f0 13 00 00         	movq	0x13f0(%rsi), %rcx
    1bd3: 48 89 ca                     	movq	%rcx, %rdx
    1bd6: 48 29 c2                     	subq	%rax, %rdx
    1bd9: 4c 39 c2                     	cmpq	%r8, %rdx
    1bdc: 0f 82 c2 00 00 00            	jb	0x1ca4 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b44>
    1be2: 8b 54 24 54                  	movl	0x54(%rsp), %edx
    1be6: 4c 01 c0                     	addq	%r8, %rax
    1be9: 48 89 86 e8 13 00 00         	movq	%rax, 0x13e8(%rsi)
    1bf0: 4d 85 c0                     	testq	%r8, %r8
    1bf3: 74 88                        	je	0x1b7d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1a1d>
    1bf5: 44 8b 8e dc 13 00 00         	movl	0x13dc(%rsi), %r9d
    1bfc: 4c 8b 54 24 68               	movq	0x68(%rsp), %r10
    1c01: 46 8b 9c 96 00 03 00 00      	movl	0x300(%rsi,%r10,4), %r11d
    1c09: 4d 0f af d9                  	imulq	%r9, %r11
    1c0d: 49 0f af d0                  	imulq	%r8, %rdx
    1c11: 49 c1 e3 03                  	shlq	$0x3, %r11
    1c15: 4c 03 1e                     	addq	(%rsi), %r11
    1c18: 4d 8b 94 24 30 20 00 00      	movq	0x2030(%r12), %r10
    1c20: 4c 8b 4c 24 48               	movq	0x48(%rsp), %r9
    1c25: 4f 8d 0c cb                  	leaq	(%r11,%r9,8), %r9
    1c29: 4d 8d 0c e9                  	leaq	(%r9,%rbp,8), %r9
    1c2d: 49 83 f8 01                  	cmpq	$0x1, %r8
    1c31: 0f 84 29 ff ff ff            	je	0x1b60 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1a00>
    1c37: 44 89 c3                     	movl	%r8d, %ebx
    1c3a: 83 e3 fe                     	andl	$-0x2, %ebx
    1c3d: 49 8d 34 d2                  	leaq	(%r10,%rdx,8), %rsi
    1c41: 48 83 c6 08                  	addq	$0x8, %rsi
    1c45: 45 31 db                     	xorl	%r11d, %r11d
    1c48: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
    1c50: f2 42 0f 10 44 de f8         	movsd	-0x8(%rsi,%r11,8), %xmm0
    1c57: 45 89 ef                     	movl	%r13d, %r15d
    1c5a: 45 0f af fb                  	imull	%r11d, %r15d
    1c5e: f2 43 0f 58 04 f9            	addsd	(%r9,%r15,8), %xmm0
    1c64: f2 43 0f 11 04 f9            	movsd	%xmm0, (%r9,%r15,8)
    1c6a: 45 89 df                     	movl	%r11d, %r15d
    1c6d: 41 83 cf 01                  	orl	$0x1, %r15d
    1c71: f2 42 0f 10 04 de            	movsd	(%rsi,%r11,8), %xmm0
    1c77: 45 0f af fd                  	imull	%r13d, %r15d
    1c7b: f2 43 0f 58 04 f9            	addsd	(%r9,%r15,8), %xmm0
    1c81: f2 43 0f 11 04 f9            	movsd	%xmm0, (%r9,%r15,8)
    1c87: 49 83 c3 02                  	addq	$0x2, %r11
    1c8b: 4c 39 db                     	cmpq	%r11, %rbx
    1c8e: 75 c0                        	jne	0x1c50 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1af0>
    1c90: 41 f6 c0 01                  	testb	$0x1, %r8b
    1c94: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
    1c99: 0f 85 c4 fe ff ff            	jne	0x1b63 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1a03>
    1c9f: e9 d9 fe ff ff               	jmp	0x1b7d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1a1d>
    1ca4: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    1ca9: 31 ff                        	xorl	%edi, %edi
    1cab: b3 0d                        	movb	$0xd, %bl
    1cad: eb 0f                        	jmp	0x1cbe <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b5e>
    1caf: 48 8b bc 24 a0 00 00 00      	movq	0xa0(%rsp), %rdi
    1cb7: 89 c3                        	movl	%eax, %ebx
    1cb9: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    1cbe: b9 10 00 00 00               	movl	$0x10, %ecx
    1cc3: e8 00 00 00 00               	callq	0x1cc8 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b68>
    1cc8: 88 18                        	movb	%bl, (%rax)
    1cca: 48 89 78 08                  	movq	%rdi, 0x8(%rax)
    1cce: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x1cd5 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b75>
    1cd5: 48 89 c1                     	movq	%rax, %rcx
    1cd8: 45 31 c0                     	xorl	%r8d, %r8d
    1cdb: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
    1ce0: e8 00 00 00 00               	callq	0x1ce5 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b85>
    1ce5: 31 ff                        	xorl	%edi, %edi
    1ce7: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    1cec: b3 0d                        	movb	$0xd, %bl
    1cee: eb ce                        	jmp	0x1cbe <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b5e>
    1cf0: b3 0b                        	movb	$0xb, %bl
    1cf2: 48 8b bc 24 a0 00 00 00      	movq	0xa0(%rsp), %rdi
    1cfa: eb bd                        	jmp	0x1cb9 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1b59>
    1cfc: 48 8b b4 24 a0 00 00 00      	movq	0xa0(%rsp), %rsi
    1d04: b9 10 00 00 00               	movl	$0x10, %ecx
    1d09: e8 00 00 00 00               	callq	0x1d0e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1bae>
    1d0e: c6 00 0c                     	movb	$0xc, (%rax)
    1d11: eb 15                        	jmp	0x1d28 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1bc8>
    1d13: 48 8b b4 24 a0 00 00 00      	movq	0xa0(%rsp), %rsi
    1d1b: b9 10 00 00 00               	movl	$0x10, %ecx
    1d20: e8 00 00 00 00               	callq	0x1d25 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1bc5>
    1d25: c6 00 0b                     	movb	$0xb, (%rax)
    1d28: 48 89 70 08                  	movq	%rsi, 0x8(%rax)
    1d2c: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x1d33 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1bd3>
    1d33: 48 89 c1                     	movq	%rax, %rcx
    1d36: 45 31 c0                     	xorl	%r8d, %r8d
    1d39: e8 00 00 00 00               	callq	0x1d3e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1bde>
    1d3e: e8 00 00 00 00               	callq	0x1d43 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1be3>
    1d43: e9 fb 09 00 00               	jmp	0x2743 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25e3>
    1d48: 48 89 c6                     	movq	%rax, %rsi
    1d4b: e8 00 00 00 00               	callq	0x1d50 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1bf0>
    1d50: e9 f6 09 00 00               	jmp	0x274b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25eb>
    1d55: 83 fa 01                     	cmpl	$0x1, %edx
    1d58: 0f 85 d5 09 00 00            	jne	0x2733 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25d3>
    1d5e: 48 89 c1                     	movq	%rax, %rcx
    1d61: e8 00 00 00 00               	callq	0x1d66 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c06>
    1d66: 80 38 0c                     	cmpb	$0xc, (%rax)
    1d69: 0f 85 cf 09 00 00            	jne	0x273e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25de>
    1d6f: e8 00 00 00 00               	callq	0x1d74 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c14>
    1d74: 48 8b 8c 24 b0 00 00 00      	movq	0xb0(%rsp), %rcx
    1d7c: 48 8b 81 80 04 00 00         	movq	0x480(%rcx), %rax
    1d83: 48 8b 89 88 04 00 00         	movq	0x488(%rcx), %rcx
    1d8a: 48 29 c1                     	subq	%rax, %rcx
    1d8d: 48 8b bc 24 48 01 00 00      	movq	0x148(%rsp), %rdi
    1d95: 0f 84 bf 00 00 00            	je	0x1e5a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1cfa>
    1d9b: 85 ed                        	testl	%ebp, %ebp
    1d9d: 0f 84 b7 00 00 00            	je	0x1e5a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1cfa>
    1da3: 8b 96 dc 13 00 00            	movl	0x13dc(%rsi), %edx
    1da9: 4c 8b 06                     	movq	(%rsi), %r8
    1dac: 48 c1 f9 03                  	sarq	$0x3, %rcx
    1db0: 66 0f 57 c0                  	xorpd	%xmm0, %xmm0
    1db4: eb 13                        	jmp	0x1dc9 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c69>
    1db6: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
    1dc0: 48 85 c9                     	testq	%rcx, %rcx
    1dc3: 0f 84 91 00 00 00            	je	0x1e5a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1cfa>
    1dc9: 44 8b 4c c8 f8               	movl	-0x8(%rax,%rcx,8), %r9d
    1dce: 44 8b 54 c8 fc               	movl	-0x4(%rax,%rcx,8), %r10d
    1dd3: 48 ff c9                     	decq	%rcx
    1dd6: 4c 0f af ca                  	imulq	%rdx, %r9
    1dda: 4c 0f af d2                  	imulq	%rdx, %r10
    1dde: 4f 8d 0c c8                  	leaq	(%r8,%r9,8), %r9
    1de2: 4f 8d 14 d0                  	leaq	(%r8,%r10,8), %r10
    1de6: 45 31 db                     	xorl	%r11d, %r11d
    1de9: eb 21                        	jmp	0x1e0c <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1cac>
    1deb: 0f 1f 44 00 00               	nopl	(%rax,%rax)
    1df0: 66 0f 28 d9                  	movapd	%xmm1, %xmm3
    1df4: f2 0f 5c ca                  	subsd	%xmm2, %xmm1
    1df8: f2 43 0f 11 1c d9            	movsd	%xmm3, (%r9,%r11,8)
    1dfe: f2 43 0f 11 0c da            	movsd	%xmm1, (%r10,%r11,8)
    1e04: 49 ff c3                     	incq	%r11
    1e07: 4c 39 dd                     	cmpq	%r11, %rbp
    1e0a: 74 b4                        	je	0x1dc0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c60>
    1e0c: f2 43 0f 10 0c d9            	movsd	(%r9,%r11,8), %xmm1
    1e12: f2 43 0f 10 14 da            	movsd	(%r10,%r11,8), %xmm2
    1e18: 66 0f 2e c8                  	ucomisd	%xmm0, %xmm1
    1e1c: 76 12                        	jbe	0x1e30 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1cd0>
    1e1e: 66 0f 2e d0                  	ucomisd	%xmm0, %xmm2
    1e22: 77 cc                        	ja	0x1df0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c90>
    1e24: f2 0f 58 d1                  	addsd	%xmm1, %xmm2
    1e28: 66 0f 28 da                  	movapd	%xmm2, %xmm3
    1e2c: eb ca                        	jmp	0x1df8 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c98>
    1e2e: 66 90                        	nop
    1e30: 66 0f 2e d0                  	ucomisd	%xmm0, %xmm2
    1e34: 76 1a                        	jbe	0x1e50 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1cf0>
    1e36: f2 0f 58 d1                  	addsd	%xmm1, %xmm2
    1e3a: 66 0f 28 d9                  	movapd	%xmm1, %xmm3
    1e3e: 66 0f 28 ca                  	movapd	%xmm2, %xmm1
    1e42: eb b4                        	jmp	0x1df8 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c98>
    1e44: 66 66 66 2e 0f 1f 84 00 00 00 00 00  	nopw	%cs:(%rax,%rax)
    1e50: 66 0f 28 d9                  	movapd	%xmm1, %xmm3
    1e54: f2 0f 5c da                  	subsd	%xmm2, %xmm3
    1e58: eb 9e                        	jmp	0x1df8 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1c98>
    1e5a: 8b 86 d8 13 00 00            	movl	0x13d8(%rsi), %eax
    1e60: 44 8d 3c 38                  	leal	(%rax,%rdi), %r15d
    1e64: 41 c1 ef 02                  	shrl	$0x2, %r15d
    1e68: 85 c0                        	testl	%eax, %eax
    1e6a: 44 0f 44 f8                  	cmovel	%eax, %r15d
    1e6e: 45 85 ed                     	testl	%r13d, %r13d
    1e71: 0f 84 0d 08 00 00            	je	0x2684 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2524>
    1e77: 48 63 df                     	movslq	%edi, %rbx
    1e7a: 8d 47 ff                     	leal	-0x1(%rdi), %eax
    1e7d: 0f bd c8                     	bsrl	%eax, %ecx
    1e80: ff c1                        	incl	%ecx
    1e82: 85 c0                        	testl	%eax, %eax
    1e84: 0f 44 c8                     	cmovel	%eax, %ecx
    1e87: 48 0f af cf                  	imulq	%rdi, %rcx
    1e8b: 48 89 8c 24 38 01 00 00      	movq	%rcx, 0x138(%rsp)
    1e93: 48 8b 84 24 d8 00 00 00      	movq	0xd8(%rsp), %rax
    1e9b: 48 05 b0 00 00 00            	addq	$0xb0, %rax
    1ea1: 48 89 44 24 58               	movq	%rax, 0x58(%rsp)
    1ea6: 89 f8                        	movl	%edi, %eax
    1ea8: c1 e8 02                     	shrl	$0x2, %eax
    1eab: 89 84 24 d4 00 00 00         	movl	%eax, 0xd4(%rsp)
    1eb2: 8d 04 7f                     	leal	(%rdi,%rdi,2), %eax
    1eb5: c1 e8 02                     	shrl	$0x2, %eax
    1eb8: 48 8d 0c ed 00 00 00 00      	leaq	(,%rbp,8), %rcx
    1ec0: 48 89 8c 24 a8 00 00 00      	movq	%rcx, 0xa8(%rsp)
    1ec8: 48 8d 0c fd 00 00 00 00      	leaq	(,%rdi,8), %rcx
    1ed0: 48 89 8c 24 28 01 00 00      	movq	%rcx, 0x128(%rsp)
    1ed8: 45 89 fc                     	movl	%r15d, %r12d
    1edb: 48 89 84 24 30 01 00 00      	movq	%rax, 0x130(%rsp)
    1ee3: 83 c0 fe                     	addl	$-0x2, %eax
    1ee6: 89 84 24 20 01 00 00         	movl	%eax, 0x120(%rsp)
    1eed: 66 0f 28 35 50 00 00 00      	movapd	0x50(%rip), %xmm6       # 0x1f45 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1de5>
    1ef5: f2 0f 10 3d 60 00 00 00      	movsd	0x60(%rip), %xmm7       # 0x1f5d <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1dfd>
    1efd: 48 c7 84 24 c0 00 00 00 00 00 00 00  	movq	$0x0, 0xc0(%rsp)
    1f09: 48 c7 84 24 80 00 00 00 00 00 00 00  	movq	$0x0, 0x80(%rsp)
    1f15: 48 c7 84 24 b8 00 00 00 00 00 00 00  	movq	$0x0, 0xb8(%rsp)
    1f21: 31 c0                        	xorl	%eax, %eax
    1f23: 44 89 bc 24 c8 00 00 00      	movl	%r15d, 0xc8(%rsp)
    1f2b: eb 5c                        	jmp	0x1f89 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1e29>
    1f2d: 0f 1f 00                     	nopl	(%rax)
    1f30: 48 8b 84 24 a8 00 00 00      	movq	0xa8(%rsp), %rax
    1f38: 48 8b 4c 24 38               	movq	0x38(%rsp), %rcx
    1f3d: 48 8d 14 08                  	leaq	(%rax,%rcx), %rdx
    1f41: 4c 8d 04 f9                  	leaq	(%rcx,%rdi,8), %r8
    1f45: 48 8b 4c 24 68               	movq	0x68(%rsp), %rcx
    1f4a: 48 c1 e1 03                  	shlq	$0x3, %rcx
    1f4e: 48 03 4e 60                  	addq	0x60(%rsi), %rcx
    1f52: 49 29 d0                     	subq	%rdx, %r8
    1f55: e8 00 00 00 00               	callq	0x1f5a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1dfa>
    1f5a: 48 8b 84 24 88 00 00 00      	movq	0x88(%rsp), %rax
    1f62: 48 ff c0                     	incq	%rax
    1f65: 48 83 84 24 b8 00 00 00 08   	addq	$0x8, 0xb8(%rsp)
    1f6e: 48 83 84 24 80 00 00 00 10   	addq	$0x10, 0x80(%rsp)
    1f77: 48 83 84 24 c0 00 00 00 04   	addq	$0x4, 0xc0(%rsp)
    1f80: 4c 39 e8                     	cmpq	%r13, %rax
    1f83: 0f 84 fb 06 00 00            	je	0x2684 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2524>
    1f89: 8b 8e dc 13 00 00            	movl	0x13dc(%rsi), %ecx
    1f8f: 48 89 c2                     	movq	%rax, %rdx
    1f92: 48 89 4c 24 48               	movq	%rcx, 0x48(%rsp)
    1f97: 48 0f af d1                  	imulq	%rcx, %rdx
    1f9b: 4c 8b 76 40                  	movq	0x40(%rsi), %r14
    1f9f: 48 89 54 24 68               	movq	%rdx, 0x68(%rsp)
    1fa4: 48 c1 e2 04                  	shlq	$0x4, %rdx
    1fa8: 4c 01 f2                     	addq	%r14, %rdx
    1fab: 48 89 54 24 38               	movq	%rdx, 0x38(%rsp)
    1fb0: 80 bc 06 00 01 00 00 01      	cmpb	$0x1, 0x100(%rsi,%rax)
    1fb8: 48 89 84 24 88 00 00 00      	movq	%rax, 0x88(%rsp)
    1fc0: 0f 85 ca 03 00 00            	jne	0x2390 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2230>
    1fc6: 48 8b 8e e8 13 00 00         	movq	0x13e8(%rsi), %rcx
    1fcd: 48 8b 86 f0 13 00 00         	movq	0x13f0(%rsi), %rax
    1fd4: 48 29 c8                     	subq	%rcx, %rax
    1fd7: 48 8b 94 24 38 01 00 00      	movq	0x138(%rsp), %rdx
    1fdf: 48 39 c2                     	cmpq	%rax, %rdx
    1fe2: 0f 87 3c 07 00 00            	ja	0x2724 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25c4>
    1fe8: 48 8b 06                     	movq	(%rsi), %rax
    1feb: 48 01 d1                     	addq	%rdx, %rcx
    1fee: 48 89 8e e8 13 00 00         	movq	%rcx, 0x13e8(%rsi)
    1ff5: 85 ed                        	testl	%ebp, %ebp
    1ff7: 49 bb 00 00 00 00 00 00 f0 7f	movabsq	$0x7ff0000000000000, %r11 # imm = 0x7FF0000000000000
    2001: 74 52                        	je	0x2055 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1ef5>
    2003: 48 8b 8c 24 b8 00 00 00      	movq	0xb8(%rsp), %rcx
    200b: 48 0f af 4c 24 48            	imulq	0x48(%rsp), %rcx
    2011: 48 8d 14 08                  	leaq	(%rax,%rcx), %rdx
    2015: 48 03 4e 20                  	addq	0x20(%rsi), %rcx
    2019: 45 31 c0                     	xorl	%r8d, %r8d
    201c: 0f 1f 40 00                  	nopl	(%rax)
    2020: f2 42 0f 10 04 c1            	movsd	(%rcx,%r8,8), %xmm0
    2026: f2 42 0f 59 04 c2            	mulsd	(%rdx,%r8,8), %xmm0
    202c: 66 49 0f 7e c1               	movq	%xmm0, %r9
    2031: f2 42 0f 11 04 c2            	movsd	%xmm0, (%rdx,%r8,8)
    2037: 49 ba ff ff ff ff ff ff ff 7f	movabsq	$0x7fffffffffffffff, %r10 # imm = 0x7FFFFFFFFFFFFFFF
    2041: 4d 21 d1                     	andq	%r10, %r9
    2044: 4d 39 d9                     	cmpq	%r11, %r9
    2047: 0f 8d b0 06 00 00            	jge	0x26fd <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x259d>
    204d: 49 ff c0                     	incq	%r8
    2050: 4c 39 c5                     	cmpq	%r8, %rbp
    2053: 75 cb                        	jne	0x2020 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1ec0>
    2055: 48 8b 4c 24 68               	movq	0x68(%rsp), %rcx
    205a: 48 8d 04 c8                  	leaq	(%rax,%rcx,8), %rax
    205e: 49 89 fd                     	movq	%rdi, %r13
    2061: 48 8b bc 24 18 01 00 00      	movq	0x118(%rsp), %rdi
    2069: 4c 8b bc 24 10 01 00 00      	movq	0x110(%rsp), %r15
    2071: 41 0f b6 0c ff               	movzbl	(%r15,%rdi,8), %ecx
    2076: 89 ca                        	movl	%ecx, %edx
    2078: c1 e1 08                     	shll	$0x8, %ecx
    207b: 8d 0c d1                     	leal	(%rcx,%rdx,8), %ecx
    207e: 48 03 4c 24 58               	addq	0x58(%rsp), %rcx
    2083: 48 89 84 24 80 01 00 00      	movq	%rax, 0x180(%rsp)
    208b: 48 89 ac 24 88 01 00 00      	movq	%rbp, 0x188(%rsp)
    2093: 48 8b 44 24 38               	movq	0x38(%rsp), %rax
    2098: 48 89 84 24 70 01 00 00      	movq	%rax, 0x170(%rsp)
    20a0: 4c 89 ac 24 78 01 00 00      	movq	%r13, 0x178(%rsp)
    20a8: 48 8b 86 a0 00 00 00         	movq	0xa0(%rsi), %rax
    20af: 48 89 84 24 60 01 00 00      	movq	%rax, 0x160(%rsp)
    20b7: 48 8b 96 a8 00 00 00         	movq	0xa8(%rsi), %rdx
    20be: 48 29 c2                     	subq	%rax, %rdx
    20c1: 48 c1 fa 03                  	sarq	$0x3, %rdx
    20c5: 48 89 94 24 68 01 00 00      	movq	%rdx, 0x168(%rsp)
    20cd: 48 8b 86 c0 00 00 00         	movq	0xc0(%rsi), %rax
    20d4: 48 89 84 24 50 01 00 00      	movq	%rax, 0x150(%rsp)
    20dc: 48 8b 96 c8 00 00 00         	movq	0xc8(%rsi), %rdx
    20e3: 48 29 c2                     	subq	%rax, %rdx
    20e6: 48 c1 fa 03                  	sarq	$0x3, %rdx
    20ea: 48 89 94 24 58 01 00 00      	movq	%rdx, 0x158(%rsp)
    20f2: 48 8b 86 f8 13 00 00         	movq	0x13f8(%rsi), %rax
    20f9: 48 89 44 24 28               	movq	%rax, 0x28(%rsp)
    20fe: 48 8d 84 24 50 01 00 00      	leaq	0x150(%rsp), %rax
    2106: 48 89 44 24 20               	movq	%rax, 0x20(%rsp)
    210b: 48 8d 94 24 80 01 00 00      	leaq	0x180(%rsp), %rdx
    2113: 4c 8d 84 24 70 01 00 00      	leaq	0x170(%rsp), %r8
    211b: 4c 8d 8c 24 60 01 00 00      	leaq	0x160(%rsp), %r9
    2123: e8 00 00 00 00               	callq	0x2128 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1fc8>
    2128: 48 8b 84 24 d8 00 00 00      	movq	0xd8(%rsp), %rax
    2130: 8b 40 08                     	movl	0x8(%rax), %eax
    2133: 41 80 3c ff 00               	cmpb	$0x0, (%r15,%rdi,8)
    2138: 44 89 ef                     	movl	%r13d, %edi
    213b: 0f 45 f8                     	cmovnel	%eax, %edi
    213e: 80 bc 24 e8 00 00 00 00      	cmpb	$0x0, 0xe8(%rsp)
    2146: 89 fd                        	movl	%edi, %ebp
    2148: 41 0f 45 ed                  	cmovnel	%r13d, %ebp
    214c: 80 bc 24 ec 00 00 00 00      	cmpb	$0x0, 0xec(%rsp)
    2154: 41 0f 45 fd                  	cmovnel	%r13d, %edi
    2158: 89 e9                        	movl	%ebp, %ecx
    215a: c1 e9 02                     	shrl	$0x2, %ecx
    215d: 45 31 ed                     	xorl	%r13d, %r13d
    2160: 39 c5                        	cmpl	%eax, %ebp
    2162: 0f 95 c2                     	setne	%dl
    2165: 39 c7                        	cmpl	%eax, %edi
    2167: 0f 95 84 24 e0 00 00 00      	setne	0xe0(%rsp)
    216f: 8b 84 24 d4 00 00 00         	movl	0xd4(%rsp), %eax
    2176: 41 89 c7                     	movl	%eax, %r15d
    2179: 41 29 cf                     	subl	%ecx, %r15d
    217c: 74 1d                        	je	0x219b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x203b>
    217e: 4e 8d 04 fd 00 00 00 00      	leaq	(,%r15,8), %r8
    2186: 48 8b 4c 24 38               	movq	0x38(%rsp), %rcx
    218b: 88 54 24 78                  	movb	%dl, 0x78(%rsp)
    218f: 31 d2                        	xorl	%edx, %edx
    2191: e8 00 00 00 00               	callq	0x2196 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2036>
    2196: 0f b6 54 24 78               	movzbl	0x78(%rsp), %edx
    219b: d1 ed                        	shrl	%ebp
    219d: 41 8d 0c 2f                  	leal	(%r15,%rbp), %ecx
    21a1: 41 39 cf                     	cmpl	%ecx, %r15d
    21a4: 48 8b 6c 24 40               	movq	0x40(%rsp), %rbp
    21a9: 4c 8b 5c 24 38               	movq	0x38(%rsp), %r11
    21ae: 0f 83 6c 01 00 00            	jae	0x2320 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x21c0>
    21b4: 41 88 d5                     	movb	%dl, %r13b
    21b7: 44 89 e8                     	movl	%r13d, %eax
    21ba: 41 c1 e5 08                  	shll	$0x8, %r13d
    21be: 41 8d 44 c5 00               	leal	(%r13,%rax,8), %eax
    21c3: 48 03 44 24 58               	addq	0x58(%rsp), %rax
    21c8: 48 8b 80 e8 00 00 00         	movq	0xe8(%rax), %rax
    21cf: 89 c9                        	movl	%ecx, %ecx
    21d1: 49 89 c8                     	movq	%rcx, %r8
    21d4: 4d 29 f8                     	subq	%r15, %r8
    21d7: 4c 89 fa                     	movq	%r15, %rdx
    21da: 49 83 f8 06                  	cmpq	$0x6, %r8
    21de: 0f 82 ac 00 00 00            	jb	0x2290 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2130>
    21e4: 4a 8d 14 fd 00 00 00 00      	leaq	(,%r15,8), %rdx
    21ec: 4f 8d 0c fb                  	leaq	(%r11,%r15,8), %r9
    21f0: 4c 8d 14 c8                  	leaq	(%rax,%rcx,8), %r10
    21f4: 49 29 d2                     	subq	%rdx, %r10
    21f7: 4d 39 d1                     	cmpq	%r10, %r9
    21fa: 73 10                        	jae	0x220c <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x20ac>
    21fc: 4d 8d 0c cb                  	leaq	(%r11,%rcx,8), %r9
    2200: 4c 89 fa                     	movq	%r15, %rdx
    2203: 4c 39 c8                     	cmpq	%r9, %rax
    2206: 0f 82 84 00 00 00            	jb	0x2290 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2130>
    220c: 4d 89 c1                     	movq	%r8, %r9
    220f: 49 83 e1 fc                  	andq	$-0x4, %r9
    2213: 4b 8d 14 39                  	leaq	(%r9,%r15), %rdx
    2217: 4c 8b 94 24 80 00 00 00      	movq	0x80(%rsp), %r10
    221f: 4c 0f af 54 24 48            	imulq	0x48(%rsp), %r10
    2225: 4f 8d 14 fa                  	leaq	(%r10,%r15,8), %r10
    2229: 4d 01 f2                     	addq	%r14, %r10
    222c: 49 83 c2 10                  	addq	$0x10, %r10
    2230: 45 31 db                     	xorl	%r11d, %r11d
    2233: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
    2240: 66 42 0f 10 04 d8            	movupd	(%rax,%r11,8), %xmm0
    2246: 66 42 0f 10 4c d8 10         	movupd	0x10(%rax,%r11,8), %xmm1
    224d: 66 43 0f 10 54 da f0         	movupd	-0x10(%r10,%r11,8), %xmm2
    2254: 66 0f 59 d0                  	mulpd	%xmm0, %xmm2
    2258: 66 43 0f 10 04 da            	movupd	(%r10,%r11,8), %xmm0
    225e: 66 0f 59 c1                  	mulpd	%xmm1, %xmm0
    2262: 66 43 0f 11 54 da f0         	movupd	%xmm2, -0x10(%r10,%r11,8)
    2269: 66 43 0f 11 04 da            	movupd	%xmm0, (%r10,%r11,8)
    226f: 49 83 c3 04                  	addq	$0x4, %r11
    2273: 4d 39 d9                     	cmpq	%r11, %r9
    2276: 75 c8                        	jne	0x2240 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x20e0>
    2278: 4d 39 c8                     	cmpq	%r9, %r8
    227b: 4c 8b 5c 24 38               	movq	0x38(%rsp), %r11
    2280: 0f 84 9a 00 00 00            	je	0x2320 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x21c0>
    2286: 66 2e 0f 1f 84 00 00 00 00 00	nopw	%cs:(%rax,%rax)
    2290: 41 89 c9                     	movl	%ecx, %r9d
    2293: 41 29 d1                     	subl	%edx, %r9d
    2296: 49 89 d0                     	movq	%rdx, %r8
    2299: 41 f6 c1 01                  	testb	$0x1, %r9b
    229d: 74 1c                        	je	0x22bb <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x215b>
    229f: 49 89 d0                     	movq	%rdx, %r8
    22a2: 4d 29 f8                     	subq	%r15, %r8
    22a5: f2 42 0f 10 04 c0            	movsd	(%rax,%r8,8), %xmm0
    22ab: f2 41 0f 59 04 d3            	mulsd	(%r11,%rdx,8), %xmm0
    22b1: f2 41 0f 11 04 d3            	movsd	%xmm0, (%r11,%rdx,8)
    22b7: 4c 8d 42 01                  	leaq	0x1(%rdx), %r8
    22bb: 4c 8d 49 ff                  	leaq	-0x1(%rcx), %r9
    22bf: 4c 39 ca                     	cmpq	%r9, %rdx
    22c2: 74 5c                        	je	0x2320 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x21c0>
    22c4: 48 8b 94 24 80 00 00 00      	movq	0x80(%rsp), %rdx
    22cc: 48 0f af 54 24 48            	imulq	0x48(%rsp), %rdx
    22d2: 4c 01 f2                     	addq	%r14, %rdx
    22d5: 48 83 c2 08                  	addq	$0x8, %rdx
    22d9: 4e 8d 0c fd 00 00 00 00      	leaq	(,%r15,8), %r9
    22e1: 4c 29 c8                     	subq	%r9, %rax
    22e4: 48 83 c0 08                  	addq	$0x8, %rax
    22e8: 0f 1f 84 00 00 00 00 00      	nopl	(%rax,%rax)
    22f0: f2 42 0f 10 44 c0 f8         	movsd	-0x8(%rax,%r8,8), %xmm0
    22f7: f2 42 0f 59 44 c2 f8         	mulsd	-0x8(%rdx,%r8,8), %xmm0
    22fe: f2 42 0f 11 44 c2 f8         	movsd	%xmm0, -0x8(%rdx,%r8,8)
    2305: f2 42 0f 10 04 c0            	movsd	(%rax,%r8,8), %xmm0
    230b: f2 42 0f 59 04 c2            	mulsd	(%rdx,%r8,8), %xmm0
    2311: f2 42 0f 11 04 c2            	movsd	%xmm0, (%rdx,%r8,8)
    2317: 49 83 c0 02                  	addq	$0x2, %r8
    231b: 4c 39 c1                     	cmpq	%r8, %rcx
    231e: 75 d0                        	jne	0x22f0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2190>
    2320: 89 f9                        	movl	%edi, %ecx
    2322: c1 e9 02                     	shrl	$0x2, %ecx
    2325: 48 8b 84 24 30 01 00 00      	movq	0x130(%rsp), %rax
    232d: 29 c8                        	subl	%ecx, %eax
    232f: d1 ef                        	shrl	%edi
    2331: 44 8d 04 38                  	leal	(%rax,%rdi), %r8d
    2335: 45 89 c2                     	movl	%r8d, %r10d
    2338: 44 39 c0                     	cmpl	%r8d, %eax
    233b: 73 73                        	jae	0x23b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2250>
    233d: 89 8c 24 b0 00 00 00         	movl	%ecx, 0xb0(%rsp)
    2344: 31 d2                        	xorl	%edx, %edx
    2346: 0f b6 8c 24 e0 00 00 00      	movzbl	0xe0(%rsp), %ecx
    234e: 88 ca                        	movb	%cl, %dl
    2350: 89 d1                        	movl	%edx, %ecx
    2352: c1 e2 08                     	shll	$0x8, %edx
    2355: 8d 0c ca                     	leal	(%rdx,%rcx,8), %ecx
    2358: 48 03 4c 24 58               	addq	0x58(%rsp), %rcx
    235d: 48 8b 89 e8 00 00 00         	movq	0xe8(%rcx), %rcx
    2364: 41 89 c7                     	movl	%eax, %r15d
    2367: 4c 89 d0                     	movq	%r10, %rax
    236a: 4c 29 f8                     	subq	%r15, %rax
    236d: 48 83 f8 10                  	cmpq	$0x10, %rax
    2371: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    2376: 4c 89 54 24 78               	movq	%r10, 0x78(%rsp)
    237b: 73 45                        	jae	0x23c2 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2262>
    237d: 4c 89 fa                     	movq	%r15, %rdx
    2380: e9 ab 00 00 00               	jmp	0x2430 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x22d0>
    2385: 66 66 2e 0f 1f 84 00 00 00 00 00     	nopw	%cs:(%rax,%rax)
    2390: 85 ff                        	testl	%edi, %edi
    2392: 0f 84 6c 01 00 00            	je	0x2504 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x23a4>
    2398: 48 8b 4c 24 38               	movq	0x38(%rsp), %rcx
    239d: 31 d2                        	xorl	%edx, %edx
    239f: 4c 8b 84 24 28 01 00 00      	movq	0x128(%rsp), %r8
    23a7: e9 53 01 00 00               	jmp	0x24ff <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x239f>
    23ac: 0f 1f 40 00                  	nopl	(%rax)
    23b0: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    23b5: 44 8b bc 24 c8 00 00 00      	movl	0xc8(%rsp), %r15d
    23bd: e9 23 01 00 00               	jmp	0x24e5 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2385>
    23c2: 4d 89 f9                     	movq	%r15, %r9
    23c5: 49 f7 d1                     	notq	%r9
    23c8: 4d 01 d1                     	addq	%r10, %r9
    23cb: 44 8d 5f ff                  	leal	-0x1(%rdi), %r11d
    23cf: 45 39 cb                     	cmpl	%r9d, %r11d
    23d2: 72 54                        	jb	0x2428 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x22c8>
    23d4: 49 c1 e9 20                  	shrq	$0x20, %r9
    23d8: 75 4e                        	jne	0x2428 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x22c8>
    23da: 48 8b 54 24 38               	movq	0x38(%rsp), %rdx
    23df: 49 89 d2                     	movq	%rdx, %r10
    23e2: 4a 8d 14 fa                  	leaq	(%rdx,%r15,8), %rdx
    23e6: 44 8d 4f ff                  	leal	-0x1(%rdi), %r9d
    23ea: 4a 8d 34 c9                  	leaq	(%rcx,%r9,8), %rsi
    23ee: 48 83 c6 08                  	addq	$0x8, %rsi
    23f2: 48 39 f2                     	cmpq	%rsi, %rdx
    23f5: 0f 83 ee 01 00 00            	jae	0x25e9 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2489>
    23fb: 48 8b 54 24 78               	movq	0x78(%rsp), %rdx
    2400: 49 8d 34 d2                  	leaq	(%r10,%rdx,8), %rsi
    2404: 4d 01 f9                     	addq	%r15, %r9
    2407: 49 29 d1                     	subq	%rdx, %r9
    240a: 4e 8d 0c c9                  	leaq	(%rcx,%r9,8), %r9
    240e: 49 83 c1 08                  	addq	$0x8, %r9
    2412: 49 39 f1                     	cmpq	%rsi, %r9
    2415: 0f 83 ce 01 00 00            	jae	0x25e9 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2489>
    241b: 4c 89 fa                     	movq	%r15, %rdx
    241e: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
    2423: 4d 89 d3                     	movq	%r10, %r11
    2426: eb 08                        	jmp	0x2430 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x22d0>
    2428: 4c 89 fa                     	movq	%r15, %rdx
    242b: 4c 8b 5c 24 38               	movq	0x38(%rsp), %r11
    2430: 48 8b 44 24 78               	movq	0x78(%rsp), %rax
    2435: 29 d0                        	subl	%edx, %eax
    2437: 49 89 d1                     	movq	%rdx, %r9
    243a: a8 01                        	testb	$0x1, %al
    243c: 74 1d                        	je	0x245b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x22fb>
    243e: 89 d0                        	movl	%edx, %eax
    2440: f7 d0                        	notl	%eax
    2442: 41 01 c0                     	addl	%eax, %r8d
    2445: f2 42 0f 10 04 c1            	movsd	(%rcx,%r8,8), %xmm0
    244b: f2 41 0f 59 04 d3            	mulsd	(%r11,%rdx,8), %xmm0
    2451: f2 41 0f 11 04 d3            	movsd	%xmm0, (%r11,%rdx,8)
    2457: 4c 8d 4a 01                  	leaq	0x1(%rdx), %r9
    245b: 4c 8b 54 24 78               	movq	0x78(%rsp), %r10
    2460: 49 8d 42 ff                  	leaq	-0x1(%r10), %rax
    2464: 48 39 c2                     	cmpq	%rax, %rdx
    2467: 44 8b bc 24 c8 00 00 00      	movl	0xc8(%rsp), %r15d
    246f: 74 74                        	je	0x24e5 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2385>
    2471: 4d 89 d0                     	movq	%r10, %r8
    2474: 4d 29 c8                     	subq	%r9, %r8
    2477: 48 8b 84 24 80 00 00 00      	movq	0x80(%rsp), %rax
    247f: 48 0f af 44 24 48            	imulq	0x48(%rsp), %rax
    2485: 4a 8d 04 c8                  	leaq	(%rax,%r9,8), %rax
    2489: 4c 01 f0                     	addq	%r14, %rax
    248c: 48 83 c0 08                  	addq	$0x8, %rax
    2490: 03 bc 24 20 01 00 00         	addl	0x120(%rsp), %edi
    2497: 2b bc 24 b0 00 00 00         	subl	0xb0(%rsp), %edi
    249e: 44 29 cf                     	subl	%r9d, %edi
    24a1: 31 d2                        	xorl	%edx, %edx
    24a3: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00       	nopw	%cs:(%rax,%rax)
    24b0: 44 8d 4f 01                  	leal	0x1(%rdi), %r9d
    24b4: f2 42 0f 10 04 c9            	movsd	(%rcx,%r9,8), %xmm0
    24ba: f2 0f 59 44 d0 f8            	mulsd	-0x8(%rax,%rdx,8), %xmm0
    24c0: f2 0f 11 44 d0 f8            	movsd	%xmm0, -0x8(%rax,%rdx,8)
    24c6: 41 89 f9                     	movl	%edi, %r9d
    24c9: f2 42 0f 10 04 c9            	movsd	(%rcx,%r9,8), %xmm0
    24cf: f2 0f 59 04 d0               	mulsd	(%rax,%rdx,8), %xmm0
    24d4: f2 0f 11 04 d0               	movsd	%xmm0, (%rax,%rdx,8)
    24d9: 48 83 c2 02                  	addq	$0x2, %rdx
    24dd: 83 c7 fe                     	addl	$-0x2, %edi
    24e0: 49 39 d0                     	cmpq	%rdx, %r8
    24e3: 75 cb                        	jne	0x24b0 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2350>
    24e5: 4b 8d 0c d3                  	leaq	(%r11,%r10,8), %rcx
    24e9: 48 8b bc 24 48 01 00 00      	movq	0x148(%rsp), %rdi
    24f1: 4d 8d 04 fb                  	leaq	(%r11,%rdi,8), %r8
    24f5: 49 29 c8                     	subq	%rcx, %r8
    24f8: 4d 85 c0                     	testq	%r8, %r8
    24fb: 7e 07                        	jle	0x2504 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x23a4>
    24fd: 31 d2                        	xorl	%edx, %edx
    24ff: e8 00 00 00 00               	callq	0x2504 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x23a4>
    2504: 45 85 ff                     	testl	%r15d, %r15d
    2507: 0f 84 23 fa ff ff            	je	0x1f30 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1dd0>
    250d: 8b 86 d8 13 00 00            	movl	0x13d8(%rsi), %eax
    2513: 89 f9                        	movl	%edi, %ecx
    2515: 29 c1                        	subl	%eax, %ecx
    2517: 8d 51 03                     	leal	0x3(%rcx), %edx
    251a: 85 c9                        	testl	%ecx, %ecx
    251c: 0f 49 d1                     	cmovnsl	%ecx, %edx
    251f: c1 fa 02                     	sarl	$0x2, %edx
    2522: d1 e8                        	shrl	%eax
    2524: 48 63 ca                     	movslq	%edx, %rcx
    2527: 48 8b 94 24 b8 00 00 00      	movq	0xb8(%rsp), %rdx
    252f: 4c 8b 4c 24 48               	movq	0x48(%rsp), %r9
    2534: 49 0f af d1                  	imulq	%r9, %rdx
    2538: 48 03 56 60                  	addq	0x60(%rsi), %rdx
    253c: 4c 8b 84 24 c0 00 00 00      	movq	0xc0(%rsp), %r8
    2544: 4d 0f af c1                  	imulq	%r9, %r8
    2548: 4c 03 86 80 00 00 00         	addq	0x80(%rsi), %r8
    254f: 4c 0f af 8c 24 80 00 00 00   	imulq	0x80(%rsp), %r9
    2558: 4d 8d 0c c9                  	leaq	(%r9,%rcx,8), %r9
    255c: 4d 01 ce                     	addq	%r9, %r14
    255f: 45 31 c9                     	xorl	%r9d, %r9d
    2562: 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    	nopw	%cs:(%rax,%rax)
    2570: 66 0f 57 c0                  	xorpd	%xmm0, %xmm0
    2574: 49 39 c1                     	cmpq	%rax, %r9
    2577: 73 06                        	jae	0x257f <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x241f>
    2579: f2 42 0f 10 04 ca            	movsd	(%rdx,%r9,8), %xmm0
    257f: 49 89 ca                     	movq	%rcx, %r10
    2582: 4d 01 ca                     	addq	%r9, %r10
    2585: 41 0f 98 c3                  	sets	%r11b
    2589: 49 39 da                     	cmpq	%rbx, %r10
    258c: 41 0f 9d c2                  	setge	%r10b
    2590: 45 08 da                     	orb	%r11b, %r10b
    2593: 75 06                        	jne	0x259b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x243b>
    2595: f2 43 0f 58 04 ce            	addsd	(%r14,%r9,8), %xmm0
    259b: 66 49 0f 7e c2               	movq	%xmm0, %r10
    25a0: 49 bb ff ff ff ff ff ff ff 7f	movabsq	$0x7fffffffffffffff, %r11 # imm = 0x7FFFFFFFFFFFFFFF
    25aa: 4d 21 da                     	andq	%r11, %r10
    25ad: 49 bb ff ff ff ff ff ff ef 7f	movabsq	$0x7fefffffffffffff, %r11 # imm = 0x7FEFFFFFFFFFFFFF
    25b7: 4d 39 da                     	cmpq	%r11, %r10
    25ba: 0f 8f 3d 01 00 00            	jg	0x26fd <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x259d>
    25c0: 66 0f 28 c8                  	movapd	%xmm0, %xmm1
    25c4: 66 0f 54 ce                  	andpd	%xmm6, %xmm1
    25c8: 66 0f 2e f9                  	ucomisd	%xmm1, %xmm7
    25cc: 0f 82 2b 01 00 00            	jb	0x26fd <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x259d>
    25d2: f2 0f 5a c0                  	cvtsd2ss	%xmm0, %xmm0
    25d6: f3 43 0f 11 04 88            	movss	%xmm0, (%r8,%r9,4)
    25dc: 49 ff c1                     	incq	%r9
    25df: 4d 39 cc                     	cmpq	%r9, %r12
    25e2: 75 8c                        	jne	0x2570 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2410>
    25e4: e9 47 f9 ff ff               	jmp	0x1f30 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x1dd0>
    25e9: 49 89 c1                     	movq	%rax, %r9
    25ec: 49 83 e1 fc                  	andq	$-0x4, %r9
    25f0: 4b 8d 14 39                  	leaq	(%r9,%r15), %rdx
    25f4: 48 8b b4 24 80 00 00 00      	movq	0x80(%rsp), %rsi
    25fc: 48 0f af 74 24 48            	imulq	0x48(%rsp), %rsi
    2602: 4a 8d 34 fe                  	leaq	(%rsi,%r15,8), %rsi
    2606: 4d 8d 3c 36                  	leaq	(%r14,%rsi), %r15
    260a: 49 83 c7 10                  	addq	$0x10, %r15
    260e: 31 f6                        	xorl	%esi, %esi
    2610: 45 89 dd                     	movl	%r11d, %r13d
    2613: 66 42 0f 10 44 e9 e8         	movupd	-0x18(%rcx,%r13,8), %xmm0
    261a: 66 42 0f 10 4c e9 f8         	movupd	-0x8(%rcx,%r13,8), %xmm1
    2621: 66 0f c6 c9 01               	shufpd	$0x1, %xmm1, %xmm1      # xmm1 = xmm1[1,0]
    2626: 66 0f c6 c0 01               	shufpd	$0x1, %xmm0, %xmm0      # xmm0 = xmm0[1,0]
    262b: 66 41 0f 10 54 f7 f0         	movupd	-0x10(%r15,%rsi,8), %xmm2
    2632: 66 0f 59 d1                  	mulpd	%xmm1, %xmm2
    2636: 66 41 0f 10 0c f7            	movupd	(%r15,%rsi,8), %xmm1
    263c: 66 0f 59 c8                  	mulpd	%xmm0, %xmm1
    2640: 66 41 0f 11 54 f7 f0         	movupd	%xmm2, -0x10(%r15,%rsi,8)
    2647: 66 41 0f 11 0c f7            	movupd	%xmm1, (%r15,%rsi,8)
    264d: 48 83 c6 04                  	addq	$0x4, %rsi
    2651: 41 83 c3 fc                  	addl	$-0x4, %r11d
    2655: 49 39 f1                     	cmpq	%rsi, %r9
    2658: 75 b6                        	jne	0x2610 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x24b0>
    265a: 4c 39 c8                     	cmpq	%r9, %rax
    265d: 48 8b 74 24 60               	movq	0x60(%rsp), %rsi
    2662: 4c 8b 6c 24 30               	movq	0x30(%rsp), %r13
    2667: 4c 8b 5c 24 38               	movq	0x38(%rsp), %r11
    266c: 0f 85 be fd ff ff            	jne	0x2430 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x22d0>
    2672: 44 8b bc 24 c8 00 00 00      	movl	0xc8(%rsp), %r15d
    267a: 4c 8b 54 24 78               	movq	0x78(%rsp), %r10
    267f: e9 61 fe ff ff               	jmp	0x24e5 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x2385>
    2684: 89 be d8 13 00 00            	movl	%edi, 0x13d8(%rsi)
    268a: 44 89 be e0 13 00 00         	movl	%r15d, 0x13e0(%rsi)
    2691: 0f 28 b4 24 90 01 00 00      	movaps	0x190(%rsp), %xmm6
    2699: 0f 28 bc 24 a0 01 00 00      	movaps	0x1a0(%rsp), %xmm7
    26a1: 44 0f 28 84 24 b0 01 00 00   	movaps	0x1b0(%rsp), %xmm8
    26aa: 44 0f 28 8c 24 c0 01 00 00   	movaps	0x1c0(%rsp), %xmm9
    26b3: 44 0f 28 94 24 d0 01 00 00   	movaps	0x1d0(%rsp), %xmm10
    26bc: 44 0f 28 9c 24 e0 01 00 00   	movaps	0x1e0(%rsp), %xmm11
    26c5: 44 0f 28 a4 24 f0 01 00 00   	movaps	0x1f0(%rsp), %xmm12
    26ce: 44 0f 28 ac 24 00 02 00 00   	movaps	0x200(%rsp), %xmm13
    26d7: 44 0f 28 b4 24 10 02 00 00   	movaps	0x210(%rsp), %xmm14
    26e0: 44 0f 28 bc 24 20 02 00 00   	movaps	0x220(%rsp), %xmm15
    26e9: 48 81 c4 38 02 00 00         	addq	$0x238, %rsp            # imm = 0x238
    26f0: 5b                           	popq	%rbx
    26f1: 5d                           	popq	%rbp
    26f2: 5f                           	popq	%rdi
    26f3: 5e                           	popq	%rsi
    26f4: 41 5c                        	popq	%r12
    26f6: 41 5d                        	popq	%r13
    26f8: 41 5e                        	popq	%r14
    26fa: 41 5f                        	popq	%r15
    26fc: c3                           	retq
    26fd: b9 10 00 00 00               	movl	$0x10, %ecx
    2702: e8 00 00 00 00               	callq	0x2707 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25a7>
    2707: c6 00 0b                     	movb	$0xb, (%rax)
    270a: 48 c7 40 08 00 00 00 00      	movq	$0x0, 0x8(%rax)
    2712: 48 8d 15 00 00 00 00         	leaq	(%rip), %rdx            # 0x2719 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25b9>
    2719: 48 89 c1                     	movq	%rax, %rcx
    271c: 45 31 c0                     	xorl	%r8d, %r8d
    271f: e8 00 00 00 00               	callq	0x2724 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25c4>
    2724: b9 10 00 00 00               	movl	$0x10, %ecx
    2729: e8 00 00 00 00               	callq	0x272e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25ce>
    272e: c6 00 0d                     	movb	$0xd, (%rax)
    2731: eb d7                        	jmp	0x270a <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25aa>
    2733: 48 89 c6                     	movq	%rax, %rsi
    2736: 48 89 f1                     	movq	%rsi, %rcx
    2739: e8 00 00 00 00               	callq	0x273e <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25de>
    273e: e8 00 00 00 00               	callq	0x2743 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25e3>
    2743: 48 89 c6                     	movq	%rax, %rsi
    2746: e8 00 00 00 00               	callq	0x274b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25eb>
    274b: 48 89 f1                     	movq	%rsi, %rcx
    274e: e8 00 00 00 00               	callq	0x2753 <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25f3>
    2753: 48 89 c1                     	movq	%rax, %rcx
    2756: e8 00 00 00 00               	callq	0x275b <stx_vorbis::detail::decode_packet(stx_vorbis::detail::Workspace&, stx_vorbis::detail::Setup const&, std::__1::span<unsigned char const, 18446744073709551615ull>)+0x25fb>
    275b: cc                           	int3

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

Disassembly of section .text$_ZNSt3__16vectorIfNS_3pmr21polymorphic_allocatorIfEEE6resizeEy:

0000000000000000 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)>:
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
      2d: 0f 86 7e 01 00 00            	jbe	0x1b1 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1b1>
      33: 48 8b 46 10                  	movq	0x10(%rsi), %rax
      37: 48 29 f8                     	subq	%rdi, %rax
      3a: 48 89 c1                     	movq	%rax, %rcx
      3d: 48 c1 f9 02                  	sarq	$0x2, %rcx
      41: 48 39 ca                     	cmpq	%rcx, %rdx
      44: 0f 86 6f 01 00 00            	jbe	0x1b9 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1b9>
      4a: 48 89 d1                     	movq	%rdx, %rcx
      4d: 48 c1 e9 3e                  	shrq	$0x3e, %rcx
      51: 0f 85 8c 01 00 00            	jne	0x1e3 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1e3>
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
      86: 0f 87 5c 01 00 00            	ja	0x1e8 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1e8>
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
      bd: e8 00 00 00 00               	callq	0xc2 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0xc2>
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
      e7: 0f 84 9a 00 00 00            	je	0x187 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x187>
      ed: 49 83 c0 fc                  	addq	$-0x4, %r8
      f1: 49 89 d2                     	movq	%rdx, %r10
      f4: 49 89 eb                     	movq	%rbp, %r11
      f7: 49 83 f8 4c                  	cmpq	$0x4c, %r8
      fb: 72 73                        	jb	0x170 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x170>
      fd: 49 01 de                     	addq	%rbx, %r14
     100: 48 01 cf                     	addq	%rcx, %rdi
     103: 49 29 fe                     	subq	%rdi, %r14
     106: 49 89 d2                     	movq	%rdx, %r10
     109: 49 89 eb                     	movq	%rbp, %r11
     10c: 49 83 fe 20                  	cmpq	$0x20, %r14
     110: 72 5e                        	jb	0x170 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x170>
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
     169: 75 e5                        	jne	0x150 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x150>
     16b: 4d 39 f0                     	cmpq	%r14, %r8
     16e: 74 17                        	je	0x187 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x187>
     170: f3 41 0f 10 02               	movss	(%r10), %xmm0
     175: f3 41 0f 11 03               	movss	%xmm0, (%r11)
     17a: 49 83 c2 04                  	addq	$0x4, %r10
     17e: 49 83 c3 04                  	addq	$0x4, %r11
     182: 49 39 ca                     	cmpq	%rcx, %r10
     185: 75 e9                        	jne	0x170 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x170>
     187: 4c 8b 46 10                  	movq	0x10(%rsi), %r8
     18b: 48 89 2e                     	movq	%rbp, (%rsi)
     18e: 4c 89 7e 08                  	movq	%r15, 0x8(%rsi)
     192: 4c 89 4e 10                  	movq	%r9, 0x10(%rsi)
     196: 48 85 d2                     	testq	%rdx, %rdx
     199: 74 37                        	je	0x1d2 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1d2>
     19b: 49 29 d0                     	subq	%rdx, %r8
     19e: 48 8b 4c 24 20               	movq	0x20(%rsp), %rcx
     1a3: 48 8b 01                     	movq	(%rcx), %rax
     1a6: 41 b9 04 00 00 00            	movl	$0x4, %r9d
     1ac: ff 50 18                     	callq	*0x18(%rax)
     1af: eb 21                        	jmp	0x1d2 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1d2>
     1b1: 73 1f                        	jae	0x1d2 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1d2>
     1b3: 48 8d 3c 97                  	leaq	(%rdi,%rdx,4), %rdi
     1b7: eb 15                        	jmp	0x1ce <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1ce>
     1b9: 4b 8d 3c be                  	leaq	(%r14,%r15,4), %rdi
     1bd: 49 c1 e7 02                  	shlq	$0x2, %r15
     1c1: 4c 89 f1                     	movq	%r14, %rcx
     1c4: 31 d2                        	xorl	%edx, %edx
     1c6: 4d 89 f8                     	movq	%r15, %r8
     1c9: e8 00 00 00 00               	callq	0x1ce <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1ce>
     1ce: 48 89 7e 08                  	movq	%rdi, 0x8(%rsi)
     1d2: 48 83 c4 28                  	addq	$0x28, %rsp
     1d6: 5b                           	popq	%rbx
     1d7: 5d                           	popq	%rbp
     1d8: 5f                           	popq	%rdi
     1d9: 5e                           	popq	%rsi
     1da: 41 5c                        	popq	%r12
     1dc: 41 5d                        	popq	%r13
     1de: 41 5e                        	popq	%r14
     1e0: 41 5f                        	popq	%r15
     1e2: c3                           	retq
     1e3: e8 00 00 00 00               	callq	0x1e8 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1e8>
     1e8: e8 00 00 00 00               	callq	0x1ed <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1ed>
     1ed: 48 89 c1                     	movq	%rax, %rcx
     1f0: e8 00 00 00 00               	callq	0x1f5 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::resize(unsigned long long)+0x1f5>
     1f5: cc                           	int3

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

Disassembly of section .text$__clang_call_terminate:

0000000000000000 <__clang_call_terminate>:
       0: 48 83 ec 28                  	subq	$0x28, %rsp
       4: e8 00 00 00 00               	callq	0x9 <__clang_call_terminate+0x9>
       9: e8 00 00 00 00               	callq	0xe <__clang_call_terminate+0xe>
       e: cc                           	int3

Disassembly of section .text$_ZNSt3__16vectorIdNS_3pmr21polymorphic_allocatorIdEEE20__throw_length_errorB9nqe220108Ev:

0000000000000000 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::__throw_length_error[abi:nqe220108]()>:
       0: 48 83 ec 28                  	subq	$0x28, %rsp
       4: 48 8d 0d 68 00 00 00         	leaq	0x68(%rip), %rcx        # 0x73 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::__throw_length_error[abi:nqe220108]()+0x73>
       b: e8 00 00 00 00               	callq	0x10 <std::__1::vector<double, std::__1::pmr::polymorphic_allocator<double>>::__throw_length_error[abi:nqe220108]()+0x10>
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

Disassembly of section .text$_ZNSt3__16vectorIfNS_3pmr21polymorphic_allocatorIfEEE20__throw_length_errorB9nqe220108Ev:

0000000000000000 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::__throw_length_error[abi:nqe220108]()>:
       0: 48 83 ec 28                  	subq	$0x28, %rsp
       4: 48 8d 0d 68 00 00 00         	leaq	0x68(%rip), %rcx        # 0x73 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::__throw_length_error[abi:nqe220108]()+0x73>
       b: e8 00 00 00 00               	callq	0x10 <std::__1::vector<float, std::__1::pmr::polymorphic_allocator<float>>::__throw_length_error[abi:nqe220108]()+0x10>
      10: cc                           	int3

Disassembly of section .text$_ZNSt3__16vectorIjNS_3pmr21polymorphic_allocatorIjEEE20__throw_length_errorB9nqe220108Ev:

0000000000000000 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::__throw_length_error[abi:nqe220108]()>:
       0: 48 83 ec 28                  	subq	$0x28, %rsp
       4: 48 8d 0d 68 00 00 00         	leaq	0x68(%rip), %rcx        # 0x73 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::__throw_length_error[abi:nqe220108]()+0x73>
       b: e8 00 00 00 00               	callq	0x10 <std::__1::vector<unsigned int, std::__1::pmr::polymorphic_allocator<unsigned int>>::__throw_length_error[abi:nqe220108]()+0x10>
      10: cc                           	int3
