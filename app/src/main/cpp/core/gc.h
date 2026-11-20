// GC：非移动 mark-sweep（docs 02 章 6.8/6.9；M1 单线程 stop-the-world）
// 自动 GC 接线（docs 12 章 22.10）：Heap::Alloc 分配失败重试链触发本收集器。
#pragma once

#include <cstddef>

namespace dexvm::runtime {
struct VMState;
}

namespace dexvm::gc {

// 从 VMState 收集根（InternTable/类静态/解释器帧保守扫描/pending exception/
// 钉住根/保守 C++ 栈）并 mark-sweep；复用 Heap 空闲链。
// 返回回收字节数（used 差额）并计入 Heap GC 观测（NoteGcRun）。
// 重入（GC 过程中再触发）返回 0：单流同步 stop-the-world，无并发在跑 GC。
size_t CollectGarbage(runtime::VMState& state);

// 分配失败 GC 接线（docs 12 章 22.10 AllocateInternalWithGc）：把一轮
// CollectGarbage 挂到 state.heap 的分配失败重试链。引导点调用一次
// （state 须覆盖 heap 全部使用期）。
void GcAttach(runtime::VMState& state);

// 外部根提供者：非 guest 堆内登记表（如桥接层 ClickRegistry 里的
// listener——AOSP 中由 View.mListenerInfo 持有，本 VM 由桥接表持有）参与
// 标记。visit 对每个强引用调用 mark(mark_ctx, obj)；obj 必须是堆对象起点。
// 单槽全局（同一进程至多一个 VM 会话）；fn=nullptr 清除。
using ExternalMarkFn = void (*)(void* mark_ctx, void* obj);
using ExternalRootVisitorFn = void (*)(void* ctx, ExternalMarkFn mark, void* mark_ctx);
void SetExternalRootVisitor(ExternalRootVisitorFn fn, void* ctx);
// 追加一个外部根提供者（互不覆盖：点击登记表 + Dialog 窗口登记表等）。
// 相同 (fn, ctx) 幂等；GC 标记期持有同一互斥锁。
void AddExternalRootVisitor(ExternalRootVisitorFn fn, void* ctx);

}  // namespace dexvm::gc
