// fiber_switch.S —— 最小 fiber 上下文切换（aarch64 / x86_64）
//
// 为什么自研：Android bionic 已移除 getcontext/makecontext/swapcontext，
// macOS ucontext_t 有 ODR 陷阱（见 docs 04 章）。只保存被调用者保存寄存器。
//
// 上下文缓冲区布局（256 字节）：
//   aarch64 : x19..x28 @0..72, x29/x30 @80..88, sp @96, d8..d15 @104..159
//   x86_64  : rbx @0, rbp @8, r12..r15 @16..40, rsp @48, rip @56
//
// rsp 约定（x86_64）：保存 rip=*rsp、rsp+=8（相当于 ret 已弹出）；
// 恢复时 jmp rip，对被切走的函数等价于 swap 正常返回。

#if defined(__APPLE__)
#define C_SYM(x) _##x
#else
#define C_SYM(x) x
#endif

#if defined(__aarch64__)

  .text
  .p2align 2

  .globl C_SYM(dexvm_fiber_swap)
C_SYM(dexvm_fiber_swap):
  // x0 = save, x1 = load
  stp x19, x20, [x0, #0]
  stp x21, x22, [x0, #16]
  stp x23, x24, [x0, #32]
  stp x25, x26, [x0, #48]
  stp x27, x28, [x0, #64]
  stp x29, x30, [x0, #80]
  mov x2, sp
  str x2, [x0, #96]
  stp d8,  d9,  [x0, #104]
  stp d10, d11, [x0, #120]
  stp d12, d13, [x0, #136]
  stp d14, d15, [x0, #152]

  ldp x19, x20, [x1, #0]
  ldp x21, x22, [x1, #16]
  ldp x23, x24, [x1, #32]
  ldp x25, x26, [x1, #48]
  ldp x27, x28, [x1, #64]
  ldp x29, x30, [x1, #80]
  ldr x2, [x1, #96]
  mov sp, x2
  ldp d8,  d9,  [x1, #104]
  ldp d10, d11, [x1, #120]
  ldp d12, d13, [x1, #136]
  ldp d14, d15, [x1, #152]
  ret

  .globl C_SYM(dexvm_fiber_init)
C_SYM(dexvm_fiber_init):
  // x0 = buf, x1 = trampoline, x2 = stack_top
  stp xzr, xzr, [x0, #0]
  stp xzr, xzr, [x0, #16]
  stp xzr, xzr, [x0, #32]
  stp xzr, xzr, [x0, #48]
  stp xzr, xzr, [x0, #64]
  str xzr, [x0, #80]          // x29
  str x1, [x0, #88]           // x30 (lr) = trampoline
  and x2, x2, #~15            // sp 16 字节对齐
  str x2, [x0, #96]           // sp
  stp xzr, xzr, [x0, #104]
  stp xzr, xzr, [x0, #120]
  stp xzr, xzr, [x0, #136]
  stp xzr, xzr, [x0, #152]
  ret

#elif defined(__x86_64__)

  .text
  .p2align 4

  .globl C_SYM(dexvm_fiber_swap)
C_SYM(dexvm_fiber_swap):
  // rdi = save, rsi = load
  movq %rbx, 0(%rdi)
  movq %rbp, 8(%rdi)
  movq %r12, 16(%rdi)
  movq %r13, 24(%rdi)
  movq %r14, 32(%rdi)
  movq %r15, 40(%rdi)
  leaq 8(%rsp), %rax          // rsp 当作已弹出返回地址
  movq %rax, 48(%rdi)
  movq (%rsp), %rax           // 返回地址 = resume rip
  movq %rax, 56(%rdi)

  movq 0(%rsi), %rbx
  movq 8(%rsi), %rbp
  movq 16(%rsi), %r12
  movq 24(%rsi), %r13
  movq 32(%rsi), %r14
  movq 40(%rsi), %r15
  movq 48(%rsi), %rsp
  movq 56(%rsi), %rax
  jmpq *%rax

  .globl C_SYM(dexvm_fiber_init)
C_SYM(dexvm_fiber_init):
  // rdi = buf, rsi = trampoline, rdx = stack_top
  xorq %rax, %rax
  movq %rax, 0(%rdi)
  movq %rax, 8(%rdi)
  movq %rax, 16(%rdi)
  movq %rax, 24(%rdi)
  movq %rax, 32(%rdi)
  movq %rax, 40(%rdi)
  andq $-16, %rdx
  leaq -8(%rdx), %rax         // 模拟 call 后对齐：rsp ≡ 8 (mod 16)
  movq %rax, 48(%rdi)         // rsp
  movq %rsi, 56(%rdi)         // rip = trampoline
  ret

#else
#error "unsupported architecture for fiber_switch.S"
#endif
