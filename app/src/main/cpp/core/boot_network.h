// 启动期网络快失败开关（bridge 语义）。
//
// 加固 app 的 MainActivity.onCreate 会串行等待大量配置/网络请求；宿主网络
// 不可达或慢时，连接/超时把启动拖到 80s+。首帧前按“离线启动”语义让
// socket/DNS/TLS 立即失败，app 走本地默认配置/骨架；首帧后关闭该开关，
// 会话 tick 里恢复正常网络重试。对齐“无网络也有基础界面”。
#pragma once

namespace dexvm::runtime {

void SetBootNetworkOffline(bool offline);
bool BootNetworkOffline();

}  // namespace dexvm::runtime
