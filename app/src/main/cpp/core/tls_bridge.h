// Host TLS bridge —— guest javax.net.ssl 到宿主真 TLS 栈的接缝。
//
// 口径（bridge-first）：TLS 语义（DNS/SNI/证书链/主机名校验/协议/cipher）
// 全部由宿主 TLS 栈决定；guest lib 只做 API 薄壳，native 只做缝。
//
// 线程模型：
//   guest task（协程 carrier）  --TlsQueueXxx-->  请求队列
//   guest task                  --TlsAwaitReply--> 协程切片等待（不阻塞 carrier，
//                                                  回复到达时跨线程 wake 快速返回）
//   宿主 TLS 线程（Android: TlsBridge.java / 宿主探针: OpenSSL）
//                               --TlsTakeRequest--> 阻塞取请求
//                               --TlsDeliverReply--> 投递回复 + 唤醒等待 task
//
// 传输协议（native <-> 宿主桥接线程，小端字节序）：
//   op    = u8 kind | i64 id | i64 handle | i32 port | i32 timeout_ms |
//           i32 max_bytes | i32 host_len | i32 data_len | host bytes |
//           data bytes
//   reply = nativeTlsReply(id, status, handle, data)
//   OPEN 成功时 data 为文本元数据（UTF-8 行编码，key=value；san=type|value）；
//   native 侧在元数据前置 "handle=<n>\n" 交给 guest。
#pragma once

#include <cstdint>
#include <string>

namespace dexvm::runtime {

// op kind（与宿主桥接线程协议一致）
enum TlsOpKind : uint8_t {
  kTlsOpOpen = 1,
  kTlsOpWrite = 2,
  kTlsOpRead = 3,
  kTlsOpClose = 4,
};

// 回复状态（guest 侧映射为异常/EOF）；0 成功。
enum TlsReplyStatus : int32_t {
  kTlsOk = 0,
  kTlsEof = 1,
  kTlsIoError = 2,
  kTlsTimeout = 3,
  kTlsSslError = 4,
  kTlsConnectError = 5,
  kTlsClosed = 6,
  kTlsUnavailable = 7,
};

struct TlsRequest {
  uint8_t kind = 0;
  int64_t id = 0;
  int64_t handle = 0;  // OPEN 时无意义；其余为宿主分配的连接句柄
  std::string host;
  int32_t port = 0;
  int32_t timeout_ms = 0;  // OPEN: connect timeout；READ: SO_TIMEOUT；0=无限
  int32_t max_bytes = 0;   // READ
  std::string data;        // WRITE 字节
};

struct TlsReply {
  int32_t status = kTlsIoError;
  int64_t handle = 0;  // OPEN 成功 = 宿主连接句柄
  std::string data;    // 读数据 / 元数据 / 错误诊断
};

// 宿主桥接线程已就绪（nativeTlsTake 首次进入 / 宿主探针显式置位）。
void TlsSetBridgeAvailable(bool available);
bool TlsBridgeAvailable();

// 入队（非阻塞；返回请求 id）。
int64_t TlsQueueOpen(const std::string& host, int port, int connect_timeout_ms);
int64_t TlsQueueWrite(int64_t handle, const std::string& data);
int64_t TlsQueueRead(int64_t handle, int max_bytes, int so_timeout_ms);
int64_t TlsQueueClose(int64_t handle);

// 协程化等待回复：deadline_ms<=0 表示无期限。false = 等待超时（回复已清理）。
bool TlsAwaitReply(int64_t id, int timeout_ms, TlsReply* reply);

// 宿主桥接线程面：阻塞取请求（shutdown 且队列空返回 false）。
bool TlsTakeRequest(TlsRequest* out);
// 投递回复并唤醒等待中的 guest task（宿主线程调用；线程安全）。
void TlsDeliverReply(int64_t id, int32_t status, int64_t handle, const std::string& data);

// 编码为宿主桥接线程协议字节串（JNI nativeTlsTake 返回面 / 探针服务线程）。
std::string TlsEncodeRequest(const TlsRequest& req);

// 会话结束/VM 复位：清空残留 waiter 与未消费回复（防跨 scheduler 悬挂 task）。
void TlsResetWaiters();
// 请求宿主桥接线程退出（nativeTlsTake 返回 null）。
void TlsShutdown();
// 复位 shutdown（下次 TlsTakeRequest 重新阻塞取请求）。
void TlsResume();

}  // namespace dexvm::runtime
