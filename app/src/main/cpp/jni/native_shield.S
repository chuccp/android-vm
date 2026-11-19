// native_shield.S —— JNI native 调用的 ABI 硬墙（aarch64）
//
// 背景：加固 APK 的 packer native（实测 SecNeo libDexHelper.so 的
// Lcom/secneo/apkwrapper/H;.d）返回时改写了调用者的被调用者保存寄存器
// （x29 帧指针及编译器缓存的 TPIDR_EL0 寄存器）。-O2 下 clang 用 x29 相对
// 寻址 stack canary、用某 callee-saved 寄存器缓存 TLS 基址，native 违反
// AAPCS64 后函数尾检查立即 SIGABRT "stack corruption detected"。
//
// 官方 ART generic JNI stub 假设 native 守 ABI（native 侧出错由 native 自己
// 的 canary 暴露）；这里对不可信 native 反转假设：C++ 侧一律经本桩调用，
// 调用前保存 x19..x30 / d8..d15，返回后逐项比对（clobber_mask 供诊断）并
// 恢复寄存器，编译器看到的是普通函数调用，寄存器契约由汇编保证。
//
// 栈传参：AAPCS64 整数参数 x0..x7 寄存器用尽后，其余参数按声明序放 [sp]
// （blr 时 sp 即被调方栈参起址）。实测 CloudIDHelper.encrypt 有 7 个 String
// 形参 = env/self + 7 GPR 字，第 7 个参数必须上栈（旧实现 9 参数直接失败，
// guest 账户链路 InternalError）。本桩固定预留 8 个栈参槽（0x40 字节）。
//
// DexvmJniCallFrame 布局（与 native_manager.cc 结构体逐字段对应）：
//  0x00 fn           0x08 gpr[8]（x0..x7）
//  0x48 fpr[8]（d0..d7）  0x88 ret_x   0x90 ret_d
//  0x98 clobber_mask（bit0..9=x19..x28，bit10=x29）
//  0xa0 tp_before    0xa8 tp_after
//  0xb0 stack_args[8]（栈传参）0xf0 stack_count
//
// 本帧布局（sp 起）：
//  [0x00..0x40) 栈参（blr 时被调方可见）
//  [0x40..0x100) 被调用者保存寄存器备份
//  [0xe0] frame 指针（保存区内）

#if defined(__APPLE__)
#define C_SYM(x) _##x
#else
#define C_SYM(x) x
#endif

#if defined(__aarch64__)

  .text
  .p2align 2

  // 载入保存值并与当前寄存器比较，命中则置 mask 对应位（x9 为 mask）
  .macro SHIELD_CMP reg, off, bit
  ldr  x10, [sp, \off]
  cmp  \reg, x10
  cset x11, ne
  orr  x9, x9, x11, lsl #\bit
  .endm

  .globl C_SYM(dexvm_jni_shield_call)
C_SYM(dexvm_jni_shield_call):
  // x0 = DexvmJniCallFrame*
  sub  sp, sp, #0x140

  // 栈传参铺底：固定拷 8 槽（count 之外的槽被调方不读；值未用无副作用）
  ldp  x9, x10, [x0, #0xb0]
  stp  x9, x10, [sp, #0x00]
  ldp  x9, x10, [x0, #0xc0]
  stp  x9, x10, [sp, #0x10]
  ldp  x9, x10, [x0, #0xd0]
  stp  x9, x10, [sp, #0x20]
  ldp  x9, x10, [x0, #0xe0]
  stp  x9, x10, [sp, #0x30]

  // 被调用者保存寄存器备份（+0x40）
  stp  x19, x20, [sp, #0x40]
  stp  x21, x22, [sp, #0x50]
  stp  x23, x24, [sp, #0x60]
  stp  x25, x26, [sp, #0x70]
  stp  x27, x28, [sp, #0x80]
  stp  x29, x30, [sp, #0x90]
  stp  d8,  d9,  [sp, #0xa0]
  stp  d10, d11, [sp, #0xb0]
  stp  d12, d13, [sp, #0xc0]
  stp  d14, d15, [sp, #0xd0]
  str  x0, [sp, #0xe0]          // frame 指针（native 可能改写 x0，落栈最稳）
  mrs  x8, tpidr_el0
  str  x8, [x0, #0xa0]          // frame->tp_before

  ldr  x16, [x0, #0x00]         // fn
  ldp  x1, x2, [x0, #0x10]      // gpr[1], gpr[2]
  ldp  x3, x4, [x0, #0x20]
  ldp  x5, x6, [x0, #0x30]
  ldr  x7, [x0, #0x40]
  ldp  d0, d1, [x0, #0x48]      // fpr[0..7]
  ldp  d2, d3, [x0, #0x58]
  ldp  d4, d5, [x0, #0x68]
  ldp  d6, d7, [x0, #0x78]
  ldr  x0, [x0, #0x08]          // gpr[0]（env）最后装
  blr  x16

  // 结果落栈（frame 指针从本帧取，不信任任何寄存器）
  ldr  x17, [sp, #0xe0]
  str  x0, [x17, #0x88]         // ret_x
  str  d0, [x17, #0x90]         // ret_d（低 32 位即 float 位型）
  mrs  x8, tpidr_el0
  str  x8, [x17, #0xa8]         // tp_after

  // clobber_mask：返回后寄存器仍保留 native 返回值则合规；被改写即违规。
  mov  x9, #0
  SHIELD_CMP x19, 0x40, 0
  SHIELD_CMP x20, 0x48, 1
  SHIELD_CMP x21, 0x50, 2
  SHIELD_CMP x22, 0x58, 3
  SHIELD_CMP x23, 0x60, 4
  SHIELD_CMP x24, 0x68, 5
  SHIELD_CMP x25, 0x70, 6
  SHIELD_CMP x26, 0x78, 7
  SHIELD_CMP x27, 0x80, 8
  SHIELD_CMP x28, 0x88, 9
  SHIELD_CMP x29, 0x90, 10
  str  x9, [x17, #0x98]

  // 恢复调用者寄存器契约
  ldp  x19, x20, [sp, #0x40]
  ldp  x21, x22, [sp, #0x50]
  ldp  x23, x24, [sp, #0x60]
  ldp  x25, x26, [sp, #0x70]
  ldp  x27, x28, [sp, #0x80]
  ldp  x29, x30, [sp, #0x90]
  ldp  d8,  d9,  [sp, #0xa0]
  ldp  d10, d11, [sp, #0xb0]
  ldp  d12, d13, [sp, #0xc0]
  ldp  d14, d15, [sp, #0xd0]
  add  sp, sp, #0x140
  ret

#endif
