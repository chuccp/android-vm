// java.io（File/Stream/RandomAccessFile/PrintStream）+ java.net socket/InetAddress 面。
// 纯移动自 core/builtins.cc（零行为变更；注册/绑定仍在 registrar）。

#include "core/builtins.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <set>
#include <string>
#include <time.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include <dirent.h>
#include <ifaddrs.h>
#include <zlib.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <arpa/inet.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "core/exception.h"

#include "core/class_linker.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "session/class_loader.h"
#include "session/session.h"


#include "core/builtins_internal.h"

namespace dexvm::runtime {

// ---- java.io.PrintStream 最小面（System.out/err 语义：宿主控制台）----
void PSWrite(VMState& state, VObject* self, const std::u16string& text, bool newline) {
  (void)state;
  FILE* out = stdout;
  VField* sf = FindInstField(self != nullptr ? self->klass : nullptr, "sink");
  int32_t sink = 1;
  if (sf != nullptr && self != nullptr) {
    std::memcpy(&sink, self->field_data() + sf->offset, sizeof(sink));
  }
  if (sink == 2) out = stderr;
  std::string mutf8;
  mutf8.reserve(text.size() + 1);
  for (char16_t c : text) mutf8.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  std::fwrite(mutf8.data(), 1, mutf8.size(), out);
  if (newline) std::fputc('\n', out);
  std::fflush(out);
}
namespace {
std::u16string PSArgText(VMState& state, const Value& v, bool* ok) {
  std::u16string out;
  if (v.ref == nullptr) {
    AppendUtf16(&out, "null");
  } else if (v.ref->klass != nullptr && v.ref->klass->descriptor == "Ljava/lang/String;") {
    VString* s = static_cast<VString*>(v.ref);
    out.assign(s->chars(), s->chars() + s->length);
  } else {
    ObjectToString(state, v.ref, &out);
    if (state.HasPendingException()) {
      state.ClearException();
      AppendUtf16(&out, v.ref->klass != nullptr ? v.ref->klass->descriptor.c_str() : "?");
    }
    (void)ok;
  }
  return out;
}
}  // namespace
Value PSPrintlnNone(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  PSWrite(state, self, u"", true);
  return Value{};
}
Value PSPrintlnStr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  PSWrite(state, self, PSArgText(state, args[0], ok), true);
  return Value{};
}
Value PSPrintStr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  PSWrite(state, self, PSArgText(state, args[0], ok), false);
  return Value{};
}
Value PSPrintlnInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string out;
  AppendInt64(&out, args[0].i);
  PSWrite(state, self, out, true);
  (void)ok;
  return Value{};
}
Value PSPrintlnLong(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string out;
  AppendInt64(&out, args[0].l);
  PSWrite(state, self, out, true);
  (void)ok;
  return Value{};
}
Value PSPrintlnBool(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  PSWrite(state, self, args[0].i != 0 ? u"true" : u"false", true);
  (void)ok;
  return Value{};
}
Value PSPrintlnChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string out(1, static_cast<char16_t>(args[0].i & 0xffff));
  PSWrite(state, self, out, true);
  (void)ok;
  return Value{};
}
Value PSPrintlnDouble(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string out;
  AppendJavaDouble(&out, args[0].d, false);
  PSWrite(state, self, out, true);
  (void)ok;
  return Value{};
}
Value PSPrintlnFloat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  uint32_t bits = 0;
  std::memcpy(&bits, &args[0].i, 4);
  float f = 0;
  std::memcpy(&f, &bits, 4);
  std::u16string out;
  AppendJavaDouble(&out, f, true);
  PSWrite(state, self, out, true);
  (void)ok;
  return Value{};
}
// ---- java.io 文件 IO native 面（Q2 lib 类 VM 语义缝：POSIX 直通）----
// §31.4 归位：FileInputStream/FileDescriptor 类定义走 lib，VM 语义（文件 IO）
// 以 native 面补进 lib 类。字段契约（vm_types.h 槽面）：
//   FileInputStream.fd (Ljava/io/FileDescriptor;)；FileDescriptor.fd (I 低 4 字节)。
// 链上首用：sun.security.provider.SeedGenerator 读 /dev/urandom（SHA1PRNG 熵源）。
namespace {
VObject* FISDescriptor(VMState& state, VObject* self, bool* ok) {
  if (self == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "FileInputStream receiver");
    *ok = false;
    return nullptr;
  }
  VField* f = FindInstField(self->klass, "fd");
  VObject* fdobj = nullptr;
  if (f != nullptr) {
    std::memcpy(&fdobj, self->field_data() + f->offset, sizeof(fdobj));
  }
  if (fdobj == nullptr) {
    state.ThrowNew("Ljava/lang/IOException;", "FileInputStream is closed");
    *ok = false;
    return nullptr;
  }
  return fdobj;
}

int32_t FDHostFd(VObject* fdobj) {
  VField* f = FindInstField(fdobj->klass, "fd");
  if (f == nullptr) return -1;
  return GetIField(fdobj, f->offset);
}

void FDSetHostFd(VObject* fdobj, int32_t hostfd) {
  VField* f = FindInstField(fdobj->klass, "fd");
  if (f != nullptr) SetIField(fdobj, f->offset, hostfd);
}

}  // namespace
Value FISInitIDs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  return Value{};
}

Value FISOpen0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "open0 path");
    *ok = false;
    return Value{};
  }
  VObject* fdobj = FISDescriptor(state, self, ok);
  if (!*ok) return Value{};
  std::string path = Mutf8Arg(args[0]);
  int hostfd = ::open(path.c_str(), O_RDONLY);
  if (hostfd < 0) {
    state.ThrowNew("Ljava/io/FileNotFoundException;", path + " (" + std::strerror(errno) + ")");
    *ok = false;
    return Value{};
  }
  FDSetHostFd(fdobj, hostfd);
  return Value{};
}

Value FISRead0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VObject* fdobj = FISDescriptor(state, self, ok);
  if (!*ok) return Value{};
  int32_t hostfd = FDHostFd(fdobj);
  unsigned char b = 0;
  ssize_t r = ::read(hostfd, &b, 1);
  if (r < 0) {
    state.ThrowNew("Ljava/lang/IOException;", std::strerror(errno));
    *ok = false;
    return Value{};
  }
  Value out{};
  out.i = r == 0 ? -1 : static_cast<int32_t>(b);
  return out;
}

Value FISReadBytes(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 3 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "readBytes buffer");
    *ok = false;
    return Value{};
  }
  VObject* fdobj = FISDescriptor(state, self, ok);
  if (!*ok) return Value{};
  VArray* buf = static_cast<VArray*>(args[0].ref);
  int32_t off = args[1].i;
  int32_t len = args[2].i;
  if (off < 0 || len < 0 || off > buf->length - len) {
    state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;", "readBytes range");
    *ok = false;
    return Value{};
  }
  if (len == 0) {
    Value out{};
    out.i = 0;
    return out;
  }
  std::vector<unsigned char> tmp(static_cast<size_t>(len));
  int32_t hostfd = FDHostFd(fdobj);
  ssize_t r = ::read(hostfd, tmp.data(), static_cast<size_t>(len));
  if (r < 0) {
    state.ThrowNew("Ljava/lang/IOException;", std::strerror(errno));
    *ok = false;
    return Value{};
  }
  for (ssize_t i = 0; i < r; i++) {
    uint64_t slot = tmp[static_cast<size_t>(i)];  // kWord 槽：低字节有效
    std::memcpy(buf->data() + (static_cast<size_t>(off) + i) * 8, &slot, sizeof(slot));
  }
  Value out{};
  // EOF 口径与单字节 FISRead0 统一（Android/InputStream：len>0 且读到尾
  // 返回 -1；len==0 形已在上方返回 0）。
  out.i = r == 0 ? -1 : static_cast<int32_t>(r);
  return out;
}

Value FISSkip0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  VObject* fdobj = FISDescriptor(state, self, ok);
  if (!*ok) return Value{};
  int32_t hostfd = FDHostFd(fdobj);
  off_t before = ::lseek(hostfd, 0, SEEK_CUR);
  off_t after = ::lseek(hostfd, static_cast<off_t>(args[0].l), SEEK_CUR);
  Value out{};
  out.l = (before >= 0 && after >= 0) ? static_cast<int64_t>(after - before) : 0;
  return out;
}

Value FISAvailable0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VObject* fdobj = FISDescriptor(state, self, ok);
  if (!*ok) return Value{};
  int32_t hostfd = FDHostFd(fdobj);
  Value out{};
  struct stat st;
  if (::fstat(hostfd, &st) == 0 && S_ISREG(st.st_mode)) {
    off_t cur = ::lseek(hostfd, 0, SEEK_CUR);
    if (cur >= 0 && st.st_size > cur) out.i = static_cast<int32_t>(st.st_size - cur);
  }
  return out;
}

Value FDInitIDs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  return Value{};
}

Value FDClose0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  if (self != nullptr) {
    int32_t hostfd = FDHostFd(self);
    if (hostfd >= 0) {
      ::close(hostfd);
      FDSetHostFd(self, -1);
    }
  }
  (void)state;
  return Value{};
}

Value FDSync(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  int32_t hostfd = self != nullptr ? FDHostFd(self) : -1;
  if (hostfd < 0 || ::fsync(hostfd) != 0) {
    state.ThrowNew("Ljava/io/SyncFailedException;", std::strerror(errno));
    *ok = false;
  }
  return Value{};
}

Value FDGetHandle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.l = -1;  // POSIX 无 handle 概念
  return r;
}

Value FDGetAppend(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = 0;
  return r;
}

Value FCCleanupClose0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)ok;
  if (n >= 1 && args[0].i >= 0) {
    ::close(args[0].i);
  }
  (void)state;
  return Value{};
}

// ---- java.io.FileOutputStream native 面（java.io 文件面补全：POSIX 直通）----
// java.net socket 缝的 SocketOutputStream extends FileOutputStream（<clinit> 走
// initIDs）；文件写出面 open0/write/writeBytes 与 FileInputStream 镜像。
namespace {
VObject* FOSDescriptor(VMState& state, VObject* self, bool* ok) {
  if (self == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "FileOutputStream receiver");
    *ok = false;
    return nullptr;
  }
  VField* f = FindInstField(self->klass, "fd");
  VObject* fdobj = nullptr;
  if (f != nullptr) {
    std::memcpy(&fdobj, self->field_data() + f->offset, sizeof(fdobj));
  }
  if (fdobj == nullptr) {
    state.ThrowNew("Ljava/io/IOException;", "FileOutputStream is closed");
    *ok = false;
    return nullptr;
  }
  return fdobj;
}

}  // namespace
Value FOSOpen0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "open0 path");
    *ok = false;
    return Value{};
  }
  VObject* fdobj = FOSDescriptor(state, self, ok);
  if (!*ok) return Value{};
  std::string path = Mutf8Arg(args[0]);
  bool append = args[1].i != 0;
  int flags = O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC);
  int hostfd = ::open(path.c_str(), flags, 0666);
  if (hostfd < 0) {
    state.ThrowNew("Ljava/io/FileNotFoundException;",
                   path + " (" + std::strerror(errno) + ")");
    *ok = false;
    return Value{};
  }
  FDSetHostFd(fdobj, hostfd);
#if defined(__ANDROID__)
  // TEMP DIAG（FileDownloader EBADF 归因；事后移除）。
  static std::atomic<int> fos_open_logs{0};
  if (fos_open_logs.fetch_add(1) < 30) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmFile", "FOS.open self=%p fdobj=%p fd=%d %s",
                        static_cast<void*>(self), static_cast<void*>(fdobj), hostfd,
                        path.c_str());
  }
#endif
  return Value{};
}

Value FOSWriteByte(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VObject* fdobj = FOSDescriptor(state, self, ok);
  if (!*ok) return Value{};
  int32_t hostfd = FDHostFd(fdobj);
  unsigned char b = static_cast<unsigned char>(n >= 1 ? args[0].i & 0xff : 0);
  ssize_t r;
  do {
    r = ::write(hostfd, &b, 1);
  } while (r < 0 && errno == EINTR);
  if (r != 1) {
    state.ThrowNew("Ljava/io/IOException;", std::strerror(errno));
    *ok = false;
  }
  return Value{};
}

Value FOSWriteBytes(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 3 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "writeBytes buffer");
    *ok = false;
    return Value{};
  }
  VObject* fdobj = FOSDescriptor(state, self, ok);
  if (!*ok) return Value{};
  VArray* buf = static_cast<VArray*>(args[0].ref);
  int32_t off = args[1].i;
  int32_t len = args[2].i;
  if (off < 0 || len < 0 || off > buf->length - len) {
    state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;", "writeBytes range");
    *ok = false;
    return Value{};
  }
  if (len == 0) return Value{};
  std::vector<unsigned char> tmp(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; i++) {
    uint64_t slot = 0;
    std::memcpy(&slot, buf->data() + (static_cast<size_t>(off) + i) * 8, sizeof(slot));
    tmp[static_cast<size_t>(i)] = static_cast<unsigned char>(slot & 0xffu);
  }
  int32_t hostfd = FDHostFd(fdobj);
#if defined(__ANDROID__)
  // TEMP DIAG（FileDownloader EBADF 归因；事后移除）。
  if (hostfd < 0) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmFile",
                        "FOS.writeBytes EBADF self=%p fdobj=%p len=%d", static_cast<void*>(self),
                        static_cast<void*>(fdobj), len);
  }
#endif
  size_t sent = 0;
  while (sent < tmp.size()) {
    ssize_t r = ::write(hostfd, tmp.data() + sent, tmp.size() - sent);
    if (r < 0) {
      if (errno == EINTR) continue;
      state.ThrowNew("Ljava/io/IOException;", std::strerror(errno));
      *ok = false;
      return Value{};
    }
    sent += static_cast<size_t>(r);
  }
  return Value{};
}

// ---- java.io.File native 面（AOSP 形态：resolvePath + stat/目录 POSIX 直通）----
// §31.4 Q2：类定义走 lib，VM 语义（文件系统）以 native 面补进 lib 类。
// nativeToURI（java.net.URI 构造面）/nativeDeleteOnExit（VM 无退出泵）后置。
namespace {
std::string FileArg(const Value& v) { return Mutf8Arg(v); }

}  // namespace
Value FileResolvePath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)ok;
  std::string parent = (n >= 1 && args[0].ref != nullptr) ? FileArg(args[0]) : "";
  std::string child = (n >= 2 && args[1].ref != nullptr) ? FileArg(args[1]) : "";
  std::string joined;
  if (parent.empty()) {
    joined = child;
  } else if (child.empty()) {
    joined = parent;
  } else if (child.front() == '/') {
    joined = child;
  } else {
    joined = parent;
    if (joined.back() != '/') joined.push_back('/');
    joined += child;
  }
  // 归一化：折叠重复 '/'、消 "."、按栈退 ".."（保留根/相对首段）
  bool absolute = !joined.empty() && joined[0] == '/';
  std::vector<std::string> segs;
  size_t pos = 0;
  while (pos <= joined.size()) {
    size_t end = joined.find('/', pos);
    if (end == std::string::npos) end = joined.size();
    std::string seg = joined.substr(pos, end - pos);
    pos = end + 1;
    if (seg.empty() || seg == ".") continue;
    if (seg == "..") {
      if (!segs.empty() && segs.back() != "..") {
        segs.pop_back();
      } else if (!absolute) {
        segs.push_back(seg);
      }
      continue;
    }
    segs.push_back(seg);
  }
  std::string out = absolute ? "/" : "";
  for (size_t i = 0; i < segs.size(); i++) {
    if (i > 0) out += "/";
    out += segs[i];
  }
  if (out.empty()) out = absolute ? "/" : ".";
  VString* s = state.linker->AllocString(
      std::u16string(out.begin(), out.end()));
  Value r{};
  r.ref = s;
  return r;
}

Value FileNativeExists(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)state; (void)ok;
  struct stat st;
  Value r{};
  r.i = (n >= 1 && args[0].ref != nullptr && ::stat(FileArg(args[0]).c_str(), &st) == 0) ? 1 : 0;
  return r;
}

Value FileNativeIsFile(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)state; (void)ok;
  struct stat st;
  Value r{};
  r.i = (n >= 1 && args[0].ref != nullptr && ::stat(FileArg(args[0]).c_str(), &st) == 0 &&
         S_ISREG(st.st_mode))
            ? 1
            : 0;
  return r;
}

Value FileNativeIsDirectory(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)state; (void)ok;
  struct stat st;
  Value r{};
  r.i = (n >= 1 && args[0].ref != nullptr && ::stat(FileArg(args[0]).c_str(), &st) == 0 &&
         S_ISDIR(st.st_mode))
            ? 1
            : 0;
  return r;
}

Value FileNativeLength(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)state; (void)ok;
  struct stat st;
  Value r{};
  if (n >= 1 && args[0].ref != nullptr && ::stat(FileArg(args[0]).c_str(), &st) == 0) {
    r.l = static_cast<int64_t>(st.st_size);
  }
  return r;
}

namespace {
Value FileNativeAccessOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok,
                         int mode) {
  (void)self; (void)state; (void)ok;
  Value r{};
  r.i = (n >= 1 && args[0].ref != nullptr &&
         ::access(FileArg(args[0]).c_str(), mode) == 0)
            ? 1
            : 0;
  return r;
}

}  // namespace
Value FileNativeCanRead(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return FileNativeAccessOp(state, self, args, n, ok, R_OK);
}
Value FileNativeCanWrite(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return FileNativeAccessOp(state, self, args, n, ok, W_OK);
}
Value FileNativeCanExecute(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return FileNativeAccessOp(state, self, args, n, ok, X_OK);
}

namespace {
Value FileNativeSetMode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok,
                        mode_t bits, mode_t owner_bits) {
  (void)self; (void)ok;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) return r;
  std::string path = FileArg(args[0]);
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return r;
  mode_t mode = st.st_mode;
  bool set = args[1].i != 0;
  bool owner_only = n >= 3 && args[2].i != 0;
  mode_t target = owner_only ? owner_bits : (owner_bits | (owner_bits >> 3) | (owner_bits >> 6));
  if (set) {
    mode |= target;
  } else {
    mode &= ~target;
  }
  r.i = ::chmod(path.c_str(), mode) == 0 ? 1 : 0;
  return r;
}

}  // namespace
Value FileNativeSetReadable(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return FileNativeSetMode(state, self, args, n, ok, S_IRUSR, S_IRUSR);
}
Value FileNativeSetWritable(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return FileNativeSetMode(state, self, args, n, ok, S_IWUSR, S_IWUSR);
}
Value FileNativeSetExecutable(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return FileNativeSetMode(state, self, args, n, ok, S_IXUSR, S_IXUSR);
}

Value FileNativeLastModified(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)state; (void)ok;
  struct stat st;
  Value r{};
  if (n >= 1 && args[0].ref != nullptr && ::stat(FileArg(args[0]).c_str(), &st) == 0) {
    r.l = static_cast<int64_t>(st.st_mtime) * 1000;
  }
  return r;
}

Value FileNativeSetLastModified(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)self; (void)state; (void)ok;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) return r;
  std::string path = FileArg(args[0]);
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return r;
  struct timeval tv[2];
  tv[0].tv_sec = st.st_atime;
  tv[0].tv_usec = 0;
  tv[1].tv_sec = static_cast<time_t>(args[1].l / 1000);
  tv[1].tv_usec = static_cast<suseconds_t>((args[1].l % 1000) * 1000);
  r.i = ::utimes(path.c_str(), tv) == 0 ? 1 : 0;
  return r;
}

Value FileNativeSpace(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)state; (void)ok;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) return r;
  struct statvfs vfs;
  if (::statvfs(FileArg(args[0]).c_str(), &vfs) != 0) return r;
  uint64_t blocks = vfs.f_blocks;
  uint64_t bsize = vfs.f_frsize;
  switch (args[1].i) {
    case 1: r.l = static_cast<int64_t>(blocks * bsize); break;  // total
    case 2: r.l = static_cast<int64_t>(vfs.f_bfree * bsize); break;  // free
    default: r.l = static_cast<int64_t>(vfs.f_bavail * bsize); break;  // usable
  }
  return r;
}

Value FileNativeMkdir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)ok;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) return r;
  std::string path = FileArg(args[0]);
  if (args[1].i != 0) {
    // recursive：逐级 mkdir（已存在不报错）
    std::string acc;
    size_t pos = path.front() == '/' ? 1 : 0;
    if (path.front() == '/') acc = "/";
    while (pos <= path.size()) {
      size_t end = path.find('/', pos);
      if (end == std::string::npos) end = path.size();
      std::string seg = path.substr(pos, end - pos);
      pos = end + 1;
      if (seg.empty()) continue;
      if (!acc.empty() && acc.back() != '/') acc += "/";
      acc += seg;
      ::mkdir(acc.c_str(), 0777);
    }
    struct stat st;
    r.i = (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
  } else {
    r.i = ::mkdir(path.c_str(), 0777) == 0 ? 1 : 0;
  }
  return r;
}

Value FileNativeCreate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)state; (void)ok;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  int fd = ::open(FileArg(args[0]).c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
  if (fd >= 0) {
    ::close(fd);
    r.i = 1;
  }
  return r;
}

Value FileNativeDelete(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)state; (void)ok;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  std::string path = FileArg(args[0]);
  r.i = (::unlink(path.c_str()) == 0 || ::rmdir(path.c_str()) == 0) ? 1 : 0;
  return r;
}

Value FileNativeRename(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)state; (void)ok;
  Value r{};
  if (n < 2 || args[0].ref == nullptr || args[1].ref == nullptr) return r;
  r.i = ::rename(FileArg(args[0]).c_str(), FileArg(args[1]).c_str()) == 0 ? 1 : 0;
  return r;
}

Value FileNativeList(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  std::string path = FileArg(args[0]);
  DIR* dir = ::opendir(path.c_str());
  if (dir == nullptr) return r;  // 非目录/不可读 = null（AOSP 语义）
  // AOSP FileSystem.list 口径：返回完整路径（dir + "/" + name）。lib 侧
  // File.list() 取最后一段作为名字；File.listFiles() 直接用全路径构造子
  // File——若只返回裸名，listFiles() 得到的 File 是相对路径，exists()/
  // isFile() 全部失真（ResourceIntegrityChecker 因此误判 manifest 不存在）。
  std::vector<std::string> names;
  const bool needs_sep = !path.empty() && path.back() != '/';
  struct dirent* ent;
  while ((ent = ::readdir(dir)) != nullptr) {
    std::string name = ent->d_name;
    if (name == "." || name == "..") continue;
    names.push_back(needs_sep ? path + "/" + name : path + name);
  }
  ::closedir(dir);
  VClass* arr_class = state.linker->FindClass("[Ljava/lang/String;");
  if (arr_class == nullptr) {
    *ok = false;
    return r;
  }
  VArray* arr = state.linker->AllocArray(arr_class, static_cast<int32_t>(names.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "nativeList");
    *ok = false;
    return r;
  }
  for (size_t i = 0; i < names.size(); i++) {
    VString* s = state.linker->AllocString(
        std::u16string(names[i].begin(), names[i].end()));
    ArrSetRef(arr, static_cast<int32_t>(i), s);
  }
  r.ref = arr;
  return r;
}

// ---- java.net socket native 面（Q2 lib 类 VM 语义缝：POSIX 直通）----
// §31.7 Q2：类定义走 lib，VM 语义（TCP socket / DNS）以 native 面补进 lib 类
// （java.io 文件面 POSIX 直通先例）。字段契约（vm_types.h 槽面）：
//   SocketImpl.fd (Ljava/io/FileDescriptor;)、address (Ljava/net/InetAddress;)、
//   port/localport (I)；FileDescriptor.fd (I 低 4 字节，与 java.io 共用
//   FDHostFd/FDSetHostFd 口径——fd 生命周期两面衔接）。
// vendored native 签名按 runtime-libs java.net 源实际引用清单：
//   PlainSocketImpl 族（create/connect/bind/listen/accept/read/write/available/
//   close/shutdown/setsockopt/getsockopt/urgent）+ SocketInput/OutputStream
//   （socketRead0/socketWrite0）+ Inet4/6AddressImpl 解析面（getaddrinfo 直通）。
// 链上首用：zonfig 配置 fetch（java.net.Socket 面）。

// SocketImpl 子类的 fd 字段（沿继承链；不抛——调用侧决定语义）
namespace {
VObject* SockImplFd(VObject* self) {
  if (self == nullptr) return nullptr;
  VField* f = FindInstField(self->klass, "fd");
  VObject* fdobj = nullptr;
  if (f != nullptr) std::memcpy(&fdobj, self->field_data() + f->offset, sizeof(fdobj));
  return fdobj;
}

// 实例字段（沿继承链）引用槽读写
VObject* GetObjField(VObject* o, const char* name) {
  VField* f = FindInstField(o->klass, name);
  VObject* v = nullptr;
  if (f != nullptr) std::memcpy(&v, o->field_data() + f->offset, sizeof(v));
  return v;
}

void SetObjField(VObject* o, const char* name, VObject* v) {
  VField* f = FindInstField(o->klass, name);
  if (f != nullptr) std::memcpy(o->field_data() + f->offset, &v, sizeof(v));
}

void SetIntObjField(VObject* o, const char* name, int32_t v) {
  VField* f = FindInstField(o->klass, name);
  if (f != nullptr) SetIField(o, f->offset, v);
}

// byte[] 槽（8 字节槽低字节）读写
bool ArrGetBytes(VArray* a, int32_t off, int32_t len, uint8_t* dst) {
  for (int32_t i = 0; i < len; i++) {
    uint64_t slot = 0;
    std::memcpy(&slot, a->data() + (static_cast<size_t>(off) + i) * 8, sizeof(slot));
    dst[i] = static_cast<uint8_t>(slot & 0xffu);
  }
  return true;
}

void ArrSetBytes(VArray* a, int32_t off, int32_t len, const uint8_t* src) {
  for (int32_t i = 0; i < len; i++) {
    uint64_t slot = src[i];
    std::memcpy(a->data() + (static_cast<size_t>(off) + i) * 8, &slot, sizeof(slot));
  }
}

// [B 参数校验（返回数组；失败抛 AOSP 同形异常）
VArray* ArgByteArray(VMState& state, const Value& v, int32_t off, int32_t len, bool* ok) {
  if (v.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "byte[] argument");
    *ok = false;
    return nullptr;
  }
  VArray* a = static_cast<VArray*>(v.ref);
  if (off < 0 || len < 0 || off > a->length - len) {
    state.ThrowNew("Ljava/lang/ArrayIndexOutOfBoundsException;",
                   "len == " + std::to_string(len) + " off == " + std::to_string(off) +
                       " buffer length == " + std::to_string(a->length));
    *ok = false;
    return nullptr;
  }
  return a;
}

// InetAddress → 地址字节（走 lib getAddress() 虚调用：Inet4=4/Inet6=16 字节）
// + scope（Inet6 getScopeId()，无该方法 = 0）
bool InetAddrBytes(VMState& state, VObject* ia, std::vector<uint8_t>* out, int32_t* scope,
                   bool* ok) {
  *scope = 0;
  if (ia == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "InetAddress");
    *ok = false;
    return false;
  }
  VMethod probe;
  probe.name = "getAddress";
  probe.return_type = "[B";
  VMethod* m = ia->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) {
    state.ThrowNew("Ljava/net/SocketException;", "InetAddress without address");
    *ok = false;
    return false;
  }
  Value self_v{};
  self_v.ref = ia;
  Value r{};
  if (!InvokeMethod(state, m, &self_v, 1, &r) || !*ok) {
    *ok = false;
    return false;
  }
  VArray* arr = static_cast<VArray*>(r.ref);
  if (arr == nullptr || (arr->length != 4 && arr->length != 16)) {
    state.ThrowNew("Ljava/net/SocketException;", "InetAddress without address");
    *ok = false;
    return false;
  }
  out->resize(static_cast<size_t>(arr->length));
  ArrGetBytes(arr, 0, arr->length, out->data());
  if (arr->length == 16) {
    VMethod sp;
    sp.name = "getScopeId";
    sp.return_type = "I";
    VMethod* sm = ia->klass->FindInterfaceImpl(&sp);
    if (sm != nullptr) {
      Value sr{};
      if (InvokeMethod(state, sm, &self_v, 1, &sr) && *ok) *scope = sr.i;
    }
  }
  return true;
}

// InetAddress + port → sockaddr（v4=sockaddr_in / v6=sockaddr_in6+scope）
bool InetToSockaddr(VMState& state, VObject* ia, int32_t port, sockaddr_storage* ss,
                    socklen_t* slen, bool* ok) {
  std::vector<uint8_t> bytes;
  int32_t scope = 0;
  if (!InetAddrBytes(state, ia, &bytes, &scope, ok)) return false;
  std::memset(ss, 0, sizeof(*ss));
  if (bytes.size() == 4) {
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(static_cast<uint16_t>(port));
    std::memcpy(&sin.sin_addr, bytes.data(), 4);
    std::memcpy(ss, &sin, sizeof(sin));
    *slen = sizeof(sin);
  } else {
    sockaddr_in6 sin6{};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(static_cast<uint16_t>(port));
    std::memcpy(&sin6.sin6_addr, bytes.data(), 16);
    sin6.sin6_scope_id = static_cast<uint32_t>(scope);
    std::memcpy(ss, &sin6, sizeof(sin6));
    *slen = sizeof(sin6);
  }
  return true;
}

bool IsV4MappedV6(const sockaddr_in6& sin6) {
  static const uint8_t pfx[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
  return std::memcmp(&sin6.sin6_addr, pfx, sizeof(pfx)) == 0;
}

bool IsLoopbackV6(const sockaddr_in6& sin6) {
  static const uint8_t lo[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  return std::memcmp(&sin6.sin6_addr, lo, 16) == 0;
}

// 双栈口径适配（socketCreate 建 AF_INET6 双栈 socket；IPV6_V6ONLY=0）：
//   AF_INET6 socket + v4 目标 → v4-mapped；AF_INET socket + v6 目标 →
//   v4-mapped/::1 折回 v4，其余报 family 不支持。
bool AdaptSockaddrForSocket(int fd, sockaddr_storage* ss, socklen_t* slen, std::string* err) {
  sockaddr_storage local{};
  socklen_t llen = sizeof(local);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &llen) != 0) {
    *err = std::strerror(errno);
    return false;
  }
  int sock_family = local.ss_family;
  int target_family = ss->ss_family;
  if (sock_family == target_family) return true;
  if (sock_family == AF_INET6 && target_family == AF_INET) {
    sockaddr_in sin{};
    std::memcpy(&sin, ss, sizeof(sin));
    sockaddr_in6 sin6{};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = sin.sin_port;
    uint8_t* p = reinterpret_cast<uint8_t*>(&sin6.sin6_addr);
    p[10] = 0xff;
    p[11] = 0xff;
    std::memcpy(p + 12, &sin.sin_addr, 4);
    std::memcpy(ss, &sin6, sizeof(sin6));
    *slen = sizeof(sin6);
    return true;
  }
  if (sock_family == AF_INET && target_family == AF_INET6) {
    sockaddr_in6 sin6{};
    std::memcpy(&sin6, ss, sizeof(sin6));
    if (IsV4MappedV6(sin6)) {
      sockaddr_in sin{};
      sin.sin_family = AF_INET;
      sin.sin_port = sin6.sin6_port;
      std::memcpy(&sin.sin_addr, reinterpret_cast<const uint8_t*>(&sin6.sin6_addr) + 12, 4);
      std::memcpy(ss, &sin, sizeof(sin));
      *slen = sizeof(sin);
      return true;
    }
    if (IsLoopbackV6(sin6)) {
      sockaddr_in sin{};
      sin.sin_family = AF_INET;
      sin.sin_port = sin6.sin6_port;
      uint32_t lo = htonl(INADDR_LOOPBACK);
      std::memcpy(&sin.sin_addr, &lo, 4);
      std::memcpy(ss, &sin, sizeof(sin));
      *slen = sizeof(sin);
      return true;
    }
    *err = "address family not supported by socket";
    return false;
  }
  *err = "address family not supported by socket";
  return false;
}

// 字节 → Inet4Address(4)/Inet6Address(16, scope)（走 lib 构造器；零特判：不手搓字段）
}  // namespace
VObject* MakeInetAddressBytes(VMState& state, const uint8_t* bytes, int32_t len,
                              const char* host, int32_t scope, bool* ok);

// sockaddr → InetAddress（v4-mapped v6 折回 Inet4Address；scope 保留）
namespace {
VObject* SockaddrToInetAddress(VMState& state, const sockaddr_storage& ss, bool* ok) {
  if (ss.ss_family == AF_INET) {
    sockaddr_in sin{};
    std::memcpy(&sin, &ss, sizeof(sin));
    return MakeInetAddressBytes(state, reinterpret_cast<const uint8_t*>(&sin.sin_addr), 4,
                                nullptr, 0, ok);
  }
  sockaddr_in6 sin6{};
  std::memcpy(&sin6, &ss, sizeof(sin6));
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&sin6.sin6_addr);
  if (IsV4MappedV6(sin6)) {
    return MakeInetAddressBytes(state, p + 12, 4, nullptr, 0, ok);
  }
  return MakeInetAddressBytes(state, p, 16, nullptr,
                              static_cast<int32_t>(sin6.sin6_scope_id), ok);
}

// 字节 → Inet4Address(4)/Inet6Address(16, scope)（走 lib 构造器；零特判：不手搓字段）
}  // namespace
VObject* MakeInetAddressBytes(VMState& state, const uint8_t* bytes, int32_t len,
                              const char* host, int32_t scope, bool* ok) {
  const char* desc = (len == 4) ? "Ljava/net/Inet4Address;" : "Ljava/net/Inet6Address;";
  VClass* k = state.linker->FindClass(desc);
  if (k == nullptr || !CheckClassDefinition(state, k) ||
      !state.linker->InitializeClass(k, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", std::string("missing ") + desc);
    }
    *ok = false;
    return nullptr;
  }
  VObject* obj = state.linker->AllocObject(k);
  if (obj == nullptr) {
    ThrowOOM(state, "InetAddress alloc");
    *ok = false;
    return nullptr;
  }
  VClass* ba_cls = state.linker->FindClass("[B");
  VArray* arr = ba_cls != nullptr ? state.linker->AllocArray(ba_cls, len) : nullptr;
  if (arr == nullptr) {
    ThrowOOM(state, "InetAddress address bytes");
    *ok = false;
    return nullptr;
  }
  ArrSetBytes(arr, 0, len, bytes);
  // InvokeMethod 口径：args[0]=receiver（同 ClsNewInstance 调 ctor）
  Value args[4] = {};
  args[0].ref = obj;
  if (host != nullptr) {
    VString* s = state.linker->AllocString(std::u16string(host, host + std::strlen(host)));
    args[1].ref = s;
  }
  args[2].ref = arr;
  args[3].i = scope;
  VMethod probe;
  probe.name = "<init>";
  probe.return_type = "V";
  if (len == 4) {
    probe.param_types = {"Ljava/lang/String;", "[B"};
  } else {
    probe.param_types = {"Ljava/lang/String;", "[B", "I"};
  }
  VMethod* ctor = k->FindInterfaceImpl(&probe);
  if (ctor == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", std::string("no ctor on ") + desc);
    *ok = false;
    return nullptr;
  }
  Value r{};
  if (!InvokeMethod(state, ctor, args, len == 4 ? 3 : 4, &r) || !*ok) {
    *ok = false;
    return nullptr;
  }
  return obj;
}

// AOSP net/socket.cpp socketError 映射（异常类型 + 文案口径）
namespace {
void ThrowConnectErrno(VMState& state, int err) {
  switch (err) {
    case ETIMEDOUT:
      state.ThrowNew("Ljava/net/SocketTimeoutException;", "connect timed out");
      break;
    case ECONNREFUSED:
      state.ThrowNew("Ljava/net/ConnectException;", "Connection refused");
      break;
    case EHOSTUNREACH:
      state.ThrowNew("Ljava/net/NoRouteToHostException;", "No route to host");
      break;
    case ENETUNREACH:
      state.ThrowNew("Ljava/net/NoRouteToHostException;", "No route to network");
      break;
    default:
      state.ThrowNew("Ljava/net/SocketException;", std::strerror(err));
      break;
  }
}

void ThrowBindErrno(VMState& state, int err) {
  switch (err) {
    case EADDRINUSE:
      state.ThrowNew("Ljava/net/BindException;", "Address already in use");
      break;
    case EACCES:
      state.ThrowNew("Ljava/net/BindException;", "Permission denied");
      break;
    default:
      state.ThrowNew("Ljava/net/SocketException;", std::strerror(err));
      break;
  }
}

// PlainSocketImpl.socketCreate(boolean)：vendored create(stream=false→UDP/true→TCP)
}  // namespace
Value NetSocketCreate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  bool stream = n >= 1 && args[0].i != 0;
  VObject* fdobj = SockImplFd(self);
  if (self == nullptr || fdobj == nullptr) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return Value{};
  }
  int type = stream ? SOCK_STREAM : SOCK_DGRAM;
  int fd = ::socket(AF_INET6, type, 0);
  if (fd >= 0) {
    int off = 0;  // 双栈（v4-mapped）；失败保留 v6-only
    ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
  } else {
    fd = ::socket(AF_INET, type, 0);
  }
  if (fd < 0) {
    state.ThrowNew("Ljava/io/IOException;", std::string("socket: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  FDSetHostFd(fdobj, fd);
  return Value{};
}

// 协程化阻塞原语（定义见后：socketAccept 之前的匿名命名空间）
namespace {
int CooperativePoll(int fd, short events, long long deadline_ms);
long long MonotonicNowMs();
}  // namespace

// socketConnect(InetAddress, port, timeout)：非阻塞 connect + 协程化就绪等待
Value NetSocketConnect(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 3 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "socketConnect address");
    *ok = false;
    return Value{};
  }
  VObject* fdobj = SockImplFd(self);
  if (fdobj == nullptr) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return Value{};
  }
  int fd = FDHostFd(fdobj);
  if (fd < 0) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return Value{};
  }
  sockaddr_storage ss{};
  socklen_t slen = 0;
  if (!InetToSockaddr(state, static_cast<VObject*>(args[0].ref), args[1].i, &ss, &slen, ok)) {
    return Value{};
  }
  std::string aerr;
  if (!AdaptSockaddrForSocket(fd, &ss, &slen, &aerr)) {
    state.ThrowNew("Ljava/net/NoRouteToHostException;", aerr);
    *ok = false;
    return Value{};
  }
  int timeout = args[2].i;
  int err = 0;
  {
    // 非阻塞 connect + 协程化就绪等待（timeout>0 期限；0 = 无限）
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc;
    do {
      rc = ::connect(fd, reinterpret_cast<sockaddr*>(&ss), slen);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0 && (errno == EINPROGRESS || errno == EWOULDBLOCK)) {
      long long deadline = timeout > 0 ? MonotonicNowMs() + timeout : -1;
      int pr = CooperativePoll(fd, POLLOUT, deadline);
      if (pr == 0) {
        ::fcntl(fd, F_SETFL, flags);
        state.ThrowNew("Ljava/net/SocketTimeoutException;", "connect timed out");
        *ok = false;
        return Value{};
      }
      if (pr < 0) {
        err = errno;
      } else {
        int soerr = 0;
        socklen_t l = sizeof(soerr);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &l) == 0 && soerr != 0) {
          err = soerr;
        }
      }
    } else if (rc < 0) {
      err = errno;
    }
    ::fcntl(fd, F_SETFL, flags);
  }
  if (err != 0) {
    ThrowConnectErrno(state, err);
    *ok = false;
    return Value{};
  }
  sockaddr_storage local{};
  socklen_t llen = sizeof(local);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &llen) == 0) {
    uint16_t lport = 0;
    if (local.ss_family == AF_INET) {
      sockaddr_in sin{};
      std::memcpy(&sin, &local, sizeof(sin));
      lport = ntohs(sin.sin_port);
    } else if (local.ss_family == AF_INET6) {
      sockaddr_in6 sin6{};
      std::memcpy(&sin6, &local, sizeof(sin6));
      lport = ntohs(sin6.sin6_port);
    }
    SetIntObjField(self, "localport", lport);
  }
  return Value{};
}

// socketBind(InetAddress, port)：address=null = 通配地址
Value NetSocketBind(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  VObject* fdobj = SockImplFd(self);
  if (fdobj == nullptr) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return Value{};
  }
  int fd = FDHostFd(fdobj);
  if (fd < 0) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return Value{};
  }
  sockaddr_storage ss{};
  socklen_t slen = 0;
  if (args[0].ref != nullptr) {
    if (!InetToSockaddr(state, static_cast<VObject*>(args[0].ref), args[1].i, &ss, &slen, ok)) {
      return Value{};
    }
    std::string aerr;
    if (!AdaptSockaddrForSocket(fd, &ss, &slen, &aerr)) {
      state.ThrowNew("Ljava/net/BindException;", aerr);
      *ok = false;
      return Value{};
    }
  } else {
    sockaddr_storage local{};
    socklen_t llen = sizeof(local);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &llen) == 0 &&
        local.ss_family == AF_INET) {
      sockaddr_in sin{};
      sin.sin_family = AF_INET;
      sin.sin_port = htons(static_cast<uint16_t>(args[1].i));
      sin.sin_addr.s_addr = htonl(INADDR_ANY);
      std::memcpy(&ss, &sin, sizeof(sin));
      slen = sizeof(sin);
    } else {
      sockaddr_in6 sin6{};
      sin6.sin6_family = AF_INET6;
      sin6.sin6_port = htons(static_cast<uint16_t>(args[1].i));
      std::memcpy(&ss, &sin6, sizeof(sin6));
      slen = sizeof(sin6);
    }
  }
  int rc;
  do {
    rc = ::bind(fd, reinterpret_cast<sockaddr*>(&ss), slen);
  } while (rc < 0 && errno == EINTR);
  if (rc < 0) {
    ThrowBindErrno(state, errno);
    *ok = false;
    return Value{};
  }
  sockaddr_storage local{};
  socklen_t llen = sizeof(local);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &llen) == 0) {
    uint16_t lport = 0;
    if (local.ss_family == AF_INET) {
      sockaddr_in sin{};
      std::memcpy(&sin, &local, sizeof(sin));
      lport = ntohs(sin.sin_port);
    } else if (local.ss_family == AF_INET6) {
      sockaddr_in6 sin6{};
      std::memcpy(&sin6, &local, sizeof(sin6));
      lport = ntohs(sin6.sin6_port);
    }
    SetIntObjField(self, "localport", lport);
  }
  return Value{};
}

Value NetSocketListen(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  VObject* fdobj = SockImplFd(self);
  int fd = fdobj != nullptr ? FDHostFd(fdobj) : -1;
  if (fd < 0) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return Value{};
  }
  int backlog = args[0].i > 0 ? args[0].i : 50;
  if (::listen(fd, backlog) != 0) {
    state.ThrowNew("Ljava/io/IOException;", std::string("listen: ") + std::strerror(errno));
    *ok = false;
  }
  return Value{};
}

namespace {
// 阻塞原语协程化：poll 切片 + 调度器让出（单 carrier 下不独占执行流）。
// 返回 1=就绪 0=超时(deadline 到) -1=错误（errno 保留）。
int CooperativePoll(int fd, short events, long long deadline_ms) {
  const GuestSchedOps* ops = GuestSched();
  for (;;) {
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;
    int pr;
    do {
      pr = ::poll(&pfd, 1, 0);
    } while (pr < 0 && errno == EINTR);
    if (pr < 0) return -1;
    if (pr > 0) return 1;
    long long now = (ops != nullptr && ops->now_ms != nullptr)
                        ? ops->now_ms()
                        : std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    if (deadline_ms >= 0 && now >= deadline_ms) return 0;
    int slice = 20;
    if (deadline_ms >= 0 && deadline_ms - now < slice) {
      slice = static_cast<int>(deadline_ms - now);
    }
    if (slice <= 0) return 0;
    if (ops != nullptr) {
      ops->wait(kGuestWaitJoin, now + slice, [](void*) -> bool { return false; }, nullptr);
    } else {
      struct timespec ts{};
      ts.tv_sec = 0;
      ts.tv_nsec = slice * 1000000L;
      ::nanosleep(&ts, nullptr);
    }
  }
}

long long MonotonicNowMs() {
  const GuestSchedOps* ops = GuestSched();
  if (ops != nullptr && ops->now_ms != nullptr) return ops->now_ms();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

// socketAccept(SocketImpl s)：AOSP 形态——接受 fd 写入 s.fd（复用 implAccept 预置
// FileDescriptor），peer/local 地址与端口回填 s.address/port/localport。
Value NetSocketAccept(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "socketAccept target");
    *ok = false;
    return Value{};
  }
  VObject* s = static_cast<VObject*>(args[0].ref);
  VObject* fdobj = SockImplFd(self);
  int fd = fdobj != nullptr ? FDHostFd(fdobj) : -1;
  if (fd < 0) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return Value{};
  }
  // SO_TIMEOUT：ServerSocket.setSoTimeout 语义（AbstractPlainSocketImpl.timeout
  // 字段承载，Java 层 setOption 写入）。超时面 poll(POLLIN) 判定，到期抛
  // SocketTimeoutException（Android accept 超时口径）；0=无限等待。
  int timeout_ms = 0;
  VField* tf = FindInstField(self->klass, "timeout");
  if (tf != nullptr) {
    std::memcpy(&timeout_ms, self->field_data() + tf->offset, sizeof(timeout_ms));
  }
  // 阻塞面协程化：accept 就绪等待切片让出（其它 GuestTask 可推进；
  // SO_TIMEOUT>0 为总期限，0 = 无限等待）
  {
    long long deadline = timeout_ms > 0 ? MonotonicNowMs() + timeout_ms : -1;
    int pr = CooperativePoll(fd, POLLIN, deadline);
    if (pr == 0) {
      state.ThrowNew("Ljava/net/SocketTimeoutException;", "Accept timed out");
      *ok = false;
      return Value{};
    }
    if (pr < 0) {
      state.ThrowNew("Ljava/io/IOException;", std::string("accept poll: ") + std::strerror(errno));
      *ok = false;
      return Value{};
    }
  }
  sockaddr_storage peer{};
  socklen_t plen = sizeof(peer);
  int newfd;
  do {
    newfd = ::accept(fd, reinterpret_cast<sockaddr*>(&peer), &plen);
  } while (newfd < 0 && errno == EINTR);
  if (newfd < 0) {
    state.ThrowNew("Ljava/io/IOException;", std::string("accept: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  VObject* tfdobj = SockImplFd(s);
  if (tfdobj == nullptr) {
    VClass* fdk = state.linker->FindClass("Ljava/io/FileDescriptor;");
    tfdobj = fdk != nullptr ? state.linker->AllocObject(fdk) : nullptr;
    if (tfdobj == nullptr) {
      ::close(newfd);
      ThrowOOM(state, "accept FileDescriptor");
      *ok = false;
      return Value{};
    }
    SetObjField(s, "fd", tfdobj);
  }
  FDSetHostFd(tfdobj, newfd);
  VObject* addr = SockaddrToInetAddress(state, peer, ok);
  if (!*ok) return Value{};
  SetObjField(s, "address", addr);
  uint16_t pport = 0;
  if (peer.ss_family == AF_INET) {
    sockaddr_in sin{};
    std::memcpy(&sin, &peer, sizeof(sin));
    pport = ntohs(sin.sin_port);
  } else if (peer.ss_family == AF_INET6) {
    sockaddr_in6 sin6{};
    std::memcpy(&sin6, &peer, sizeof(sin6));
    pport = ntohs(sin6.sin6_port);
  }
  SetIntObjField(s, "port", pport);
  sockaddr_storage local{};
  socklen_t llen = sizeof(local);
  if (::getsockname(newfd, reinterpret_cast<sockaddr*>(&local), &llen) == 0) {
    uint16_t lport = 0;
    if (local.ss_family == AF_INET) {
      sockaddr_in sin{};
      std::memcpy(&sin, &local, sizeof(sin));
      lport = ntohs(sin.sin_port);
    } else if (local.ss_family == AF_INET6) {
      sockaddr_in6 sin6{};
      std::memcpy(&sin6, &local, sizeof(sin6));
      lport = ntohs(sin6.sin6_port);
    }
    SetIntObjField(s, "localport", lport);
  }
  return Value{};
}

Value NetSocketAvailable(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VObject* fdobj = SockImplFd(self);
  int fd = fdobj != nullptr ? FDHostFd(fdobj) : -1;
  Value r{};
  if (fd < 0) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return r;
  }
  int nbytes = 0;
  if (::ioctl(fd, FIONREAD, &nbytes) != 0) {
    state.ThrowNew("Ljava/io/IOException;", std::strerror(errno));
    *ok = false;
    return r;
  }
  r.i = nbytes > 0 ? nbytes : 0;
  return r;
}

// socketClose0(useDeferredClose)：deferred = shutdown 唤醒阻塞读（fd 保留给在飞
// 使用者，真 close 由 releaseFD→socketClose 走 false 分支）；false = 真关（幂等）。
Value NetSocketClose0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n; (void)ok;
  bool deferred = n >= 1 && args[0].i != 0;
  VObject* fdobj = SockImplFd(self);
  if (fdobj == nullptr) return Value{};
  int fd = FDHostFd(fdobj);
  if (fd < 0) return Value{};
  if (deferred) {
    ::shutdown(fd, SHUT_RDWR);
  } else {
    ::close(fd);
    FDSetHostFd(fdobj, -1);
  }
  (void)state;
  return Value{};
}

Value NetSocketShutdown(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)ok;
  VObject* fdobj = SockImplFd(self);
  int fd = fdobj != nullptr ? FDHostFd(fdobj) : -1;
  if (fd >= 0) {
    int howto = n >= 1 ? args[0].i : 2;
    ::shutdown(fd, howto == 0 ? SHUT_RD : howto == 1 ? SHUT_WR : SHUT_RDWR);
  }
  (void)state;
  return Value{};
}

// socketSetOption0(cmd, on, value)：SocketOptions 常量 → setsockopt 映射
// （SO_TIMEOUT 由 Java 层 timeout 字段承载，native 面 no-op——AOSP 同口径）。
Value NetSocketSetOption0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 2) {
    *ok = true;
    return Value{};
  }
  VObject* fdobj = SockImplFd(self);
  int fd = fdobj != nullptr ? FDHostFd(fdobj) : -1;
  if (fd < 0) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return Value{};
  }
  int cmd = args[0].i;
  bool on = args[1].i != 0;
  VObject* val = n >= 3 ? static_cast<VObject*>(args[2].ref) : nullptr;
  auto int_val = [&](int32_t* out) -> bool {
    if (val == nullptr) return false;
    Value v{};
    return UnboxValue(state, val, 'I', &v) && (*out = v.i, true);
  };
  int rc = -1;
  int err = 0;
  switch (cmd) {
    case 0x0001: {  // TCP_NODELAY
      int v = on ? 1 : 0;
      rc = ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v));
      break;
    }
    case 0x04: {  // SO_REUSEADDR
      int v = on ? 1 : 0;
      rc = ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
      break;
    }
    case 0x0E: {  // SO_REUSEPORT
#ifdef SO_REUSEPORT
      int v = on ? 1 : 0;
      rc = ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(v));
#else
      err = ENOPROTOOPT;
#endif
      break;
    }
    case 0x0008: {  // SO_KEEPALIVE
      int v = on ? 1 : 0;
      rc = ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &v, sizeof(v));
      break;
    }
    case 0x1003: {  // SO_OOBINLINE
      int v = on ? 1 : 0;
      rc = ::setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &v, sizeof(v));
      break;
    }
    case 0x0020: {  // SO_BROADCAST
      int v = on ? 1 : 0;
      rc = ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &v, sizeof(v));
      break;
    }
    case 0x0080: {  // SO_LINGER（Boolean=false 关闭；Integer=秒）
      struct linger lg{};
      if (on) {
        int32_t secs = 0;
        if (!int_val(&secs)) {
          state.ThrowNew("Ljava/net/SocketException;", "Bad parameter for SO_LINGER");
          *ok = false;
          return Value{};
        }
        lg.l_onoff = 1;
        lg.l_linger = secs;
      }
      rc = ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
      break;
    }
    case 0x1001:  // SO_SNDBUF
    case 0x1002: {  // SO_RCVBUF
      int32_t v = 0;
      if (!int_val(&v)) {
        state.ThrowNew("Ljava/net/SocketException;", "bad parameter for buffer size");
        *ok = false;
        return Value{};
      }
      rc = ::setsockopt(fd, SOL_SOCKET, cmd == 0x1001 ? SO_SNDBUF : SO_RCVBUF, &v, sizeof(v));
      break;
    }
    case 0x3: {  // IP_TOS（v6 socket 走 IPV6_TOS）
      int32_t v = 0;
      if (!int_val(&v)) {
        state.ThrowNew("Ljava/net/SocketException;", "bad argument for IP_TOS");
        *ok = false;
        return Value{};
      }
      sockaddr_storage local{};
      socklen_t llen = sizeof(local);
      ::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &llen);
      if (local.ss_family == AF_INET6) {
#ifdef IPV6_TOS
        rc = ::setsockopt(fd, IPPROTO_IPV6, IPV6_TOS, &v, sizeof(v));
#else
        rc = ::setsockopt(fd, IPPROTO_IP, IP_TOS, &v, sizeof(v));
#endif
      } else {
        rc = ::setsockopt(fd, IPPROTO_IP, IP_TOS, &v, sizeof(v));
      }
      break;
    }
    case 0x1006:  // SO_TIMEOUT（Java 层 timeout 字段语义；native no-op）
      *ok = true;
      return Value{};
    default:
      state.ThrowNew("Ljava/net/SocketException;", "unrecognized TCP option: " +
                                                        std::to_string(cmd));
      *ok = false;
      return Value{};
  }
  if (rc != 0) {
    err = err != 0 ? err : errno;
    state.ThrowNew("Ljava/net/SocketException;", std::strerror(err));
    *ok = false;
  }
  return Value{};
}

// socketGetOption(opt, iaContainerObj)：Java 面口径——布尔项 -1=off/值=on；
// SO_LINGER -1=off/秒数；SO_BINDADDR 填 InetAddressContainer.addr 返回 0。
Value NetSocketGetOption(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VObject* fdobj = SockImplFd(self);
  int fd = fdobj != nullptr ? FDHostFd(fdobj) : -1;
  Value r{};
  r.i = -1;
  if (fd < 0) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket is closed");
    *ok = false;
    return r;
  }
  int opt = n >= 1 ? args[0].i : -1;
  VObject* container = n >= 2 ? static_cast<VObject*>(args[1].ref) : nullptr;
  if (opt == 0x000F) {  // SO_BINDADDR
    sockaddr_storage local{};
    socklen_t llen = sizeof(local);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &llen) != 0) {
      state.ThrowNew("Ljava/net/SocketException;", std::strerror(errno));
      *ok = false;
      return r;
    }
    VObject* addr = SockaddrToInetAddress(state, local, ok);
    if (!*ok) return r;
    if (container != nullptr) SetObjField(container, "addr", addr);
    r.i = 0;
    return r;
  }
  if (opt == 0x0080) {  // SO_LINGER
    struct linger lg{};
    socklen_t l = sizeof(lg);
    if (::getsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, &l) != 0) {
      state.ThrowNew("Ljava/net/SocketException;", std::strerror(errno));
      *ok = false;
      return r;
    }
    r.i = lg.l_onoff != 0 ? lg.l_linger : -1;
    return r;
  }
  int sys_opt;
  int level = SOL_SOCKET;
  switch (opt) {
    case 0x0001: level = IPPROTO_TCP; sys_opt = TCP_NODELAY; break;
    case 0x04: sys_opt = SO_REUSEADDR; break;
    case 0x0E: sys_opt = SO_REUSEPORT; break;
    case 0x0008: sys_opt = SO_KEEPALIVE; break;
    case 0x1003: sys_opt = SO_OOBINLINE; break;
    case 0x0020: sys_opt = SO_BROADCAST; break;
    case 0x1001: sys_opt = SO_SNDBUF; break;
    case 0x1002: sys_opt = SO_RCVBUF; break;
    case 0x3: {
      sockaddr_storage local{};
      socklen_t llen = sizeof(local);
      ::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &llen);
      if (local.ss_family == AF_INET6) {
#ifdef IPV6_TOS
        level = IPPROTO_IPV6;
        sys_opt = IPV6_TOS;
#else
        level = IPPROTO_IP;
        sys_opt = IP_TOS;
#endif
      } else {
        level = IPPROTO_IP;
        sys_opt = IP_TOS;
      }
      break;
    }
    default:
      return r;  // Java 面 default 不下发；-1 防御
  }
  int v = 0;
  socklen_t l = sizeof(v);
  if (::getsockopt(fd, level, sys_opt, &v, &l) != 0) {
    state.ThrowNew("Ljava/net/SocketException;", std::strerror(errno));
    *ok = false;
    return r;
  }
  bool booleanish = opt != 0x1001 && opt != 0x1002 && opt != 0x3;
  r.i = booleanish ? (v != 0 ? v : -1) : v;
  return r;
}

Value NetSocketSendUrgentData(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  VObject* fdobj = SockImplFd(self);
  int fd = fdobj != nullptr ? FDHostFd(fdobj) : -1;
  if (fd < 0) {
    state.ThrowNew("Ljava/io/IOException;", "Socket closed");
    *ok = false;
    return Value{};
  }
  unsigned char b = static_cast<unsigned char>(n >= 1 ? args[0].i & 0xff : 0);
  ssize_t r;
  do {
    r = ::send(fd, &b, 1, MSG_OOB);
  } while (r < 0 && errno == EINTR);
  if (r < 0) {
    state.ThrowNew("Ljava/net/SocketException;", std::strerror(errno));
    *ok = false;
  }
  return Value{};
}

// SocketInputStream.socketRead0(fd, b, off, len, timeout)：timeout>0 = poll 限时，
// 到期 SocketTimeoutException（AOSP 文案）；EOF 返 0（Java 面转 -1）；
// ECONNRESET → sun.net.ConnectionResetException（Java 面 catch 转译）。
Value NetSocketRead0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  if (n < 5 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "socketRead0 fd");
    *ok = false;
    return Value{};
  }
  int fd = FDHostFd(static_cast<VObject*>(args[0].ref));
  if (fd < 0) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket closed");
    *ok = false;
    return Value{};
  }
  int32_t off = args[2].i;
  int32_t len = args[3].i;
  int32_t timeout = args[4].i;
  VArray* buf = ArgByteArray(state, args[1], off, len, ok);
  if (!*ok) return Value{};
  if (len == 0) {
    Value r{};
    r.i = 0;
    return r;
  }
  {
    // 读就绪等待协程化（timeout>0 期限；0 = 阻塞式无限等待亦让出切片）
    long long deadline = timeout > 0 ? MonotonicNowMs() + timeout : -1;
    int pr = CooperativePoll(fd, POLLIN, deadline);
    if (pr == 0) {
      state.ThrowNew("Ljava/net/SocketTimeoutException;", "Read timed out");
      *ok = false;
      return Value{};
    }
    if (pr < 0) {
      state.ThrowNew("Ljava/io/IOException;", std::strerror(errno));
      *ok = false;
      return Value{};
    }
  }
  std::vector<unsigned char> tmp(static_cast<size_t>(len));
  ssize_t r;
  do {
    r = ::recv(fd, tmp.data(), static_cast<size_t>(len), 0);
  } while (r < 0 && errno == EINTR);
  if (r < 0) {
    int err = errno;
    if (err == ECONNRESET) {
      state.ThrowNew("Lsun/net/ConnectionResetException;", "Connection reset");
    } else {
      state.ThrowNew("Ljava/io/IOException;", std::strerror(err));
    }
    *ok = false;
    return Value{};
  }
  if (r > 0) ArrSetBytes(buf, off, static_cast<int32_t>(r), tmp.data());
  Value out{};
  out.i = static_cast<int32_t>(r);
  return out;
}

// SocketOutputStream.socketWrite0(fd, b, off, len)：全量写（EPIPE/ECONNRESET →
// SocketException，Java 面 catch 转 "Socket closed"）。
Value NetSocketWrite0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  if (n < 4 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "socketWrite0 fd");
    *ok = false;
    return Value{};
  }
  int fd = FDHostFd(static_cast<VObject*>(args[0].ref));
  if (fd < 0) {
    state.ThrowNew("Ljava/net/SocketException;", "Socket closed");
    *ok = false;
    return Value{};
  }
  int32_t off = args[2].i;
  int32_t len = args[3].i;
  VArray* buf = ArgByteArray(state, args[1], off, len, ok);
  if (!*ok) return Value{};
  if (len == 0) return Value{};
  std::vector<unsigned char> tmp(static_cast<size_t>(len));
  ArrGetBytes(buf, off, len, tmp.data());
  size_t sent = 0;
  while (sent < tmp.size()) {
    ssize_t r = ::send(fd, tmp.data() + sent, tmp.size() - sent, 0);
    if (r < 0) {
      if (errno == EINTR) continue;
      int err = errno;
      if (err == EPIPE) {
        state.ThrowNew("Ljava/net/SocketException;", "Broken pipe");
      } else if (err == ECONNRESET) {
        state.ThrowNew("Ljava/net/SocketException;", "Connection reset");
      } else {
        state.ThrowNew("Ljava/net/SocketException;", std::strerror(err));
      }
      *ok = false;
      return Value{};
    }
    sent += static_cast<size_t>(r);
  }
  return Value{};
}

// ---- InetAddress 解析面（Inet4/6AddressImpl 共用；getaddrinfo/getnameinfo 直通）----

Value NetGetLocalHostName(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok) {
  (void)self; (void)args; (void)n;
  char buf[256];
  Value r{};
  if (::gethostname(buf, sizeof(buf)) != 0) {
    state.ThrowNew("Ljava/net/UnknownHostException;", std::strerror(errno));
    *ok = false;
    return r;
  }
  buf[sizeof(buf) - 1] = '\0';
  r.ref = state.linker->AllocString(std::u16string(buf, buf + std::strlen(buf)));
  return r;
}

Value NetLookupAllHostAddr(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/net/UnknownHostException;", "null hostname");
    *ok = false;
    return Value{};
  }
  std::string host = Mutf8Arg(args[0]);
  struct addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &res);
  if (rc != 0 || res == nullptr) {
    std::string msg = host;
    if (rc != 0) {
      msg += ": ";
      msg += ::gai_strerror(rc);
    }
    state.ThrowNew("Ljava/net/UnknownHostException;", msg);
    *ok = false;
    return Value{};
  }
  std::vector<std::vector<uint8_t>> addrs;
  std::vector<int32_t> scopes;
  for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
    if (p->ai_family == AF_INET && p->ai_addrlen >= sizeof(sockaddr_in)) {
      sockaddr_in sin{};
      std::memcpy(&sin, p->ai_addr, sizeof(sin));
      addrs.emplace_back(reinterpret_cast<const uint8_t*>(&sin.sin_addr),
                         reinterpret_cast<const uint8_t*>(&sin.sin_addr) + 4);
      scopes.push_back(0);
    } else if (p->ai_family == AF_INET6 && p->ai_addrlen >= sizeof(sockaddr_in6)) {
      sockaddr_in6 sin6{};
      std::memcpy(&sin6, p->ai_addr, sizeof(sin6));
      const uint8_t* p6 = reinterpret_cast<const uint8_t*>(&sin6.sin6_addr);
      if (IsV4MappedV6(sin6)) {  // 归一化 v4-mapped（与 socketAccept 同口径）
        addrs.emplace_back(p6 + 12, p6 + 16);
        scopes.push_back(0);
      } else {
        addrs.emplace_back(p6, p6 + 16);
        scopes.push_back(static_cast<int32_t>(sin6.sin6_scope_id));
      }
    }
  }
  ::freeaddrinfo(res);
  if (addrs.empty()) {
    state.ThrowNew("Ljava/net/UnknownHostException;", host);
    *ok = false;
    return Value{};
  }
  VClass* arr_class = state.linker->FindClass("[Ljava/net/InetAddress;");
  if (arr_class == nullptr) {
    *ok = false;
    return Value{};
  }
  VArray* arr = state.linker->AllocArray(arr_class, static_cast<int32_t>(addrs.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "lookupAllHostAddr");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < addrs.size(); i++) {
    VObject* ia = MakeInetAddressBytes(state, addrs[i].data(),
                                       static_cast<int32_t>(addrs[i].size()), host.c_str(),
                                       scopes[i], ok);
    if (!*ok) return Value{};
    ArrSetRef(arr, static_cast<int32_t>(i), ia);
  }
  Value r{};
  r.ref = arr;
  return r;
}

Value NetGetHostByAddr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  if (n < 1) {
    *ok = true;
    return Value{};
  }
  int32_t alen = args[0].ref != nullptr ? static_cast<VArray*>(args[0].ref)->length : 0;
  VArray* in = ArgByteArray(state, args[0], 0, alen, ok);
  if (!*ok) return Value{};
  if (alen != 4 && alen != 16) {
    state.ThrowNew("Ljava/net/UnknownHostException;", "invalid address length");
    *ok = false;
    return Value{};
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(alen));
  ArrGetBytes(in, 0, alen, bytes.data());
  sockaddr_storage ss{};
  socklen_t slen = 0;
  std::memset(&ss, 0, sizeof(ss));
  if (alen == 4) {
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    std::memcpy(&sin.sin_addr, bytes.data(), 4);
    std::memcpy(&ss, &sin, sizeof(sin));
    slen = sizeof(sin);
  } else {
    sockaddr_in6 sin6{};
    sin6.sin6_family = AF_INET6;
    std::memcpy(&sin6.sin6_addr, bytes.data(), 16);
    std::memcpy(&ss, &sin6, sizeof(sin6));
    slen = sizeof(sin6);
  }
  char host[NI_MAXHOST];
  int rc = ::getnameinfo(reinterpret_cast<sockaddr*>(&ss), slen, host, sizeof(host), nullptr, 0,
                         NI_NAMEREQD);
  Value r{};
  if (rc != 0) {
    state.ThrowNew("Ljava/net/UnknownHostException;",
                   std::string(::gai_strerror(rc)));
    *ok = false;
    return r;
  }
  r.ref = state.linker->AllocString(std::u16string(host, host + std::strlen(host)));
  return r;
}

// isReachable0（ICMP 需原始套接字权限面；java.net 规范回退 TCP echo 端口 7 探测，
// 连通 true / 否则 false——不伪造成功）。
namespace {
Value NetIsReachable0Common(VMState& state, const Value* args, bool* ok, size_t addr_idx,
                            size_t timeout_idx) {
  Value r{};
  r.i = 0;
  int32_t alen = (addr_idx < 5 && args[addr_idx].ref != nullptr)
                     ? static_cast<VArray*>(args[addr_idx].ref)->length
                     : 0;
  if (alen != 4 && alen != 16) return r;
  std::vector<uint8_t> bytes(static_cast<size_t>(alen));
  ArrGetBytes(static_cast<VArray*>(args[addr_idx].ref), 0, alen, bytes.data());
  int32_t timeout = args[timeout_idx].i;
  if (timeout <= 0) timeout = 1000;
  sockaddr_storage ss{};
  socklen_t slen = 0;
  std::memset(&ss, 0, sizeof(ss));
  if (alen == 4) {
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(7);
    std::memcpy(&sin.sin_addr, bytes.data(), 4);
    std::memcpy(&ss, &sin, sizeof(sin));
    slen = sizeof(sin);
  } else {
    sockaddr_in6 sin6{};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(7);
    std::memcpy(&sin6.sin6_addr, bytes.data(), 16);
    std::memcpy(&ss, &sin6, sizeof(sin6));
    slen = sizeof(sin6);
  }
  int fd = ::socket(ss.ss_family, SOCK_STREAM, 0);
  if (fd < 0) return r;
  int flags = ::fcntl(fd, F_GETFL, 0);
  ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&ss), slen);
  if (rc < 0 && errno == EINPROGRESS) {
    struct pollfd pfd{fd, POLLOUT, 0};
    int pr = ::poll(&pfd, 1, timeout);
    if (pr > 0) {
      int soerr = 0;
      socklen_t l = sizeof(soerr);
      ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &l);
      rc = soerr == 0 ? 0 : -1;
    } else {
      rc = -1;
    }
  }
  ::close(fd);
  r.i = rc == 0 ? 1 : 0;
  (void)ok;
  return r;
}

}  // namespace
Value NetIsReachable0V4(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)n;
  return NetIsReachable0Common(state, args, ok, 0, 1);
}

Value NetIsReachable0V6(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)n;
  return NetIsReachable0Common(state, args, ok, 0, 2);
}

Value NetIsIPv6Supported(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
  r.i = fd >= 0 ? 1 : 0;
  if (fd >= 0) ::close(fd);
  return r;
}

Value NetIsReusePortAvailable0(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = 0;
#ifdef SO_REUSEPORT
  int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd >= 0) {
    int one = 1;
    r.i = ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) == 0 ? 1 : 0;
    ::close(fd);
  }
#endif
  return r;
}

// java.lang.StringUTF16.isBigEndian（lib native 面：byte[] UTF16 打包字节序探测；
// ---- java.io.RandomAccessFile native 面（sun-compat：path 键静态直通——
// vendored RandomAccessFile 无 fd 持有语义，逐调用 open/pread/pwrite/close）----
// 语义（java.io.RandomAccessFile 公开口径）：nativePrepare 按模式校验/建文件
// （writable="rw*" 缺则建；"r" 须存在）；nativeReadAt EOF 返回 -1（vendored
// read()/readFully/EOFException 契约）；nativeSetLength = ftruncate。

Value RafNativePrepare(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "nativePrepare path");
    *ok = false;
    return r;
  }
  std::string path = Mutf8Arg(args[0]);
  bool writable = args[1].i != 0;
  int flags = writable ? (O_RDWR | O_CREAT) : O_RDONLY;
  int fd = ::open(path.c_str(), flags, 0666);
  r.i = fd >= 0 ? 1 : 0;
  if (fd >= 0) ::close(fd);
  *ok = true;
  return r;
}

Value RafNativeSize(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "nativeSize path");
    *ok = false;
    return r;
  }
  std::string path = Mutf8Arg(args[0]);
  struct stat st;
  r.l = ::stat(path.c_str(), &st) == 0 ? static_cast<int64_t>(st.st_size) : 0;
  *ok = true;
  return r;
}

Value RafNativeReadAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  Value r{};
  if (n < 5 || args[0].ref == nullptr || args[2].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "nativeReadAt args");
    *ok = false;
    return r;
  }
  std::string path = Mutf8Arg(args[0]);
  int64_t pos = args[1].l;
  VArray* target = reinterpret_cast<VArray*>(args[2].ref);
  int32_t off = args[3].i;
  int32_t len = args[4].i;
  if (off < 0 || len < 0 || off > target->length || len > target->length - off) {
    state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;", "nativeReadAt range");
    *ok = false;
    return r;
  }
  if (len == 0) {
    r.i = 0;
    *ok = true;
    return r;
  }
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    r.i = -1;  // 读不到 = EOF 口径（vendored read() 契约）
    *ok = true;
    return r;
  }
  std::vector<uint8_t> buf(static_cast<size_t>(len));
  ssize_t got = ::pread(fd, buf.data(), static_cast<size_t>(len), static_cast<off_t>(pos));
  ::close(fd);
  if (got <= 0) {
    r.i = -1;
    *ok = true;
    return r;
  }
  ArrSetBytes(target, off, static_cast<int32_t>(got), buf.data());
  r.i = static_cast<int32_t>(got);
  *ok = true;
  return r;
}

Value RafNativeWriteAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  Value r{};
  if (n < 5 || args[0].ref == nullptr || args[2].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "nativeWriteAt args");
    *ok = false;
    return r;
  }
  std::string path = Mutf8Arg(args[0]);
  int64_t pos = args[1].l;
  VArray* source = reinterpret_cast<VArray*>(args[2].ref);
  int32_t off = args[3].i;
  int32_t len = args[4].i;
  if (off < 0 || len < 0 || off > source->length || len > source->length - off) {
    state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;", "nativeWriteAt range");
    *ok = false;
    return r;
  }
  std::vector<uint8_t> buf(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; i++) {
    uint64_t slot = 0;
    std::memcpy(&slot, source->data() + static_cast<size_t>(off + i) * 8, sizeof(slot));
    buf[static_cast<size_t>(i)] = static_cast<uint8_t>(slot & 0xffu);
  }
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT, 0666);
  if (fd < 0) {
    state.ThrowNew("Ljava/io/IOException;", "nativeWriteAt " + path + ": " + std::strerror(errno));
    *ok = false;
    return r;
  }
  ssize_t wrote = ::pwrite(fd, buf.data(), buf.size(), static_cast<off_t>(pos));
  ::close(fd);
  r.i = wrote < 0 ? 0 : static_cast<int32_t>(wrote);
  *ok = true;
  return r;
}

// RandomAccessFile.nativeGetFD：lib getFD 惰性建 fd。返回绑定宿主 fd 的
// FileDescriptor，供 FileOutputStream(FileDescriptor) 承接写入
// （FileDownloader 写盘链/分段下载）。
Value RafNativeGetFD(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "nativeGetFD path");
    *ok = false;
    return r;
  }
  std::string path = Mutf8Arg(args[0]);
  bool writable = args[1].i != 0;
  int hostfd = ::open(path.c_str(), writable ? (O_RDWR | O_CREAT) : O_RDONLY, 0666);
  if (hostfd < 0) {
    state.ThrowNew("Ljava/io/IOException;", path + " (" + std::strerror(errno) + ")");
    *ok = false;
    return r;
  }
  VClass* fdc = state.linker != nullptr ? state.linker->FindClass("Ljava/io/FileDescriptor;")
                                        : nullptr;
  VObject* fdobj = fdc != nullptr ? state.linker->AllocObject(fdc) : nullptr;
  if (fdobj == nullptr) {
    ::close(hostfd);
    state.ThrowNew("Ljava/io/IOException;", "FileDescriptor alloc failed");
    *ok = false;
    return r;
  }
  FDSetHostFd(fdobj, hostfd);
  r.ref = fdobj;
  return r;
}

// RandomAccessFile.nativeSeekFd：lseek 共享 fd（AOSP RAF.seek 语义）。
// FOS(fd).write 走 ::write，写入偏移随本调用移动。
Value RafNativeSeekFd(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "nativeSeekFd fd");
    *ok = false;
    return Value{};
  }
  VObject* fdobj = static_cast<VObject*>(args[0].ref);
  int32_t hostfd = FDHostFd(fdobj);
  if (hostfd < 0) {
    state.ThrowNew("Ljava/io/IOException;", "Stream Closed");
    *ok = false;
    return Value{};
  }
  if (::lseek(hostfd, static_cast<off_t>(args[1].l), SEEK_SET) < 0) {
    state.ThrowNew("Ljava/io/IOException;", std::strerror(errno));
    *ok = false;
  }
  return Value{};
}

Value RafNativeSetLength(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "nativeSetLength path");
    *ok = false;
    return r;
  }
  std::string path = Mutf8Arg(args[0]);
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT, 0666);
  if (fd < 0) {
    state.ThrowNew("Ljava/io/IOException;", "nativeSetLength " + path + ": " + std::strerror(errno));
    *ok = false;
    return r;
  }
  ::ftruncate(fd, static_cast<off_t>(args[1].l));
  ::close(fd);
  *ok = true;
  return r;
}

// ---- java.nio.channels.FileChannel nativeMap（mmap 面：POSIX mmap 直通）----
// §31.7 Q2：类定义走 lib（java.nio.channels.FileChannel），VM 语义（文件映射）
// 以 native 面补进 lib 类（file 面先例）。语义（java.nio FileChannel.map 口径）：
// 按 path 打开文件，offset 页对齐 mmap（可写 = MAP_SHARED 写穿文件 / 只读 =
// MAP_PRIVATE），映射后关闭 fd（映射随进程存活）；返回 lib java.nio.DirectByteBuffer
// （address = 映射地址 + 页内偏移，cap = size；数据面 = lib 地址形字节访问）。
Value FCNativeMap(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  Value r{};
  if (n < 4 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "nativeMap path");
    *ok = false;
    return r;
  }
  std::string path = Mutf8Arg(args[0]);
  bool writable = args[1].i != 0;
  int64_t pos = args[2].l;
  int64_t size = args[3].l;
  if (pos < 0 || size < 0 || size > 0x7fffffffLL) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "nativeMap range");
    *ok = false;
    return r;
  }
  long page = ::sysconf(_SC_PAGESIZE);
  if (page <= 0) page = 4096;
  int64_t map_off = pos & ~(static_cast<int64_t>(page) - 1);
  int64_t delta = pos - map_off;
  int64_t map_len = size + delta;
  if (map_len == 0) map_len = page;  // 空映射占位（cap=0；数据区不可达）
  int fd = ::open(path.c_str(), writable ? O_RDWR : O_RDONLY);
  if (fd < 0) {
    state.ThrowNew("Ljava/io/IOException;",
                   "nativeMap open " + path + ": " + std::strerror(errno));
    *ok = false;
    return r;
  }
  void* mapped = ::mmap(nullptr, static_cast<size_t>(map_len), PROT_READ | (writable ? PROT_WRITE : 0),
                        writable ? MAP_SHARED : MAP_PRIVATE, fd, static_cast<off_t>(map_off));
  ::close(fd);
  if (mapped == MAP_FAILED) {
    state.ThrowNew("Ljava/io/IOException;",
                   "nativeMap mmap " + path + ": " + std::strerror(errno));
    *ok = false;
    return r;
  }
  int64_t user_addr = static_cast<int64_t>(reinterpret_cast<uintptr_t>(mapped)) + delta;

  // lib java.nio.DirectByteBuffer(long addr, int cap, Object ob) 例化（数据面
  // 在 lib：Buffer.address 绝对地址 + Unsafe 地址形字节缝）
  VClass* dbb = state.linker->FindClass("Ljava/nio/DirectByteBuffer;");
  if (dbb == nullptr || !CheckClassDefinition(state, dbb) ||
      !state.linker->InitializeClass(dbb, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", "java.nio.DirectByteBuffer missing");
    }
    *ok = false;
    return r;
  }
  VMethod* ctor = nullptr;
  for (auto& m : dbb->direct_methods) {
    if (m.name == "<init>" && m.param_types.size() == 3 && m.param_types[0] == "J" &&
        m.param_types[1] == "I" && m.param_types[2] == "Ljava/lang/Object;") {
      ctor = &m;
    }
  }
  VObject* buf = state.linker->AllocObject(dbb);
  if (ctor == nullptr || buf == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "DirectByteBuffer ctor missing");
    *ok = false;
    return r;
  }
  Value cargs[4] = {};
  cargs[0].ref = buf;
  cargs[1].l = user_addr;
  cargs[2].i = static_cast<int32_t>(size);
  cargs[3].ref = nullptr;
  Value ignored{};
  if (!InvokeMethod(state, ctor, cargs, 4, &ignored)) {
    *ok = false;
    return r;
  }
  r.ref = buf;
  *ok = true;
  return r;
}

// ByteBuffer.allocateDirect(int)：匿名 mmap + lib java.nio.DirectByteBuffer
// （数据面同一地址形 Unsafe 缝；释放面后置——进程/考卷生命周期内持有）。
Value ByteBufferAllocateDirect(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)self;
  int32_t cap = n > 0 ? args[0].i : 0;
  if (cap < 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "capacity < 0");
    *ok = false;
    return Value{};
  }
  size_t len = cap > 0 ? static_cast<size_t>(cap) : 1;
  void* mem = ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) {
    ThrowOOM(state, "allocateDirect");
    *ok = false;
    return Value{};
  }
  VClass* dbb = state.linker->FindClass("Ljava/nio/DirectByteBuffer;");
  if (dbb == nullptr || !CheckClassDefinition(state, dbb) ||
      !state.linker->InitializeClass(dbb, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", "java.nio.DirectByteBuffer missing");
    }
    *ok = false;
    return Value{};
  }
  VMethod* ctor = nullptr;
  for (auto& m : dbb->direct_methods) {
    if (m.name == "<init>" && m.param_types.size() == 3 && m.param_types[0] == "J" &&
        m.param_types[1] == "I" && m.param_types[2] == "Ljava/lang/Object;") {
      ctor = &m;
    }
  }
  VObject* buf = state.linker->AllocObject(dbb);
  if (ctor == nullptr || buf == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "DirectByteBuffer ctor missing");
    *ok = false;
    return Value{};
  }
  Value cargs[4] = {};
  cargs[0].ref = buf;
  cargs[1].l = static_cast<int64_t>(reinterpret_cast<uintptr_t>(mem));
  cargs[2].i = cap;
  cargs[3].ref = nullptr;
  Value ignored{};
  if (!InvokeMethod(state, ctor, cargs, 4, &ignored)) {
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = buf;
  *ok = true;
  return r;
}

// ---- java.net.NetworkInterface native 面（ifaddrs 枚举；AOSP 形态）----
namespace {
struct IfaceInfo {
  std::string name;
  int index = 0;
  std::vector<VObject*> addrs;
};

std::vector<IfaceInfo> CollectInterfaces(VMState& state, bool* ok) {
  std::vector<IfaceInfo> out;
  struct ifaddrs* list = nullptr;
  if (::getifaddrs(&list) != 0) {
    *ok = true;  // 无可枚举接口：空集（JDK 语义）
    return out;
  }
  for (struct ifaddrs* it = list; it != nullptr; it = it->ifa_next) {
    if (it->ifa_name == nullptr || it->ifa_addr == nullptr) continue;
    int family = it->ifa_addr->sa_family;
    if (family != AF_INET && family != AF_INET6) continue;
    VObject* addr = nullptr;
    if (family == AF_INET) {
      auto* sin = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
      addr = MakeInetAddressBytes(state, reinterpret_cast<const uint8_t*>(&sin->sin_addr), 4,
                                  nullptr, 0, ok);
    } else {
      auto* sin6 = reinterpret_cast<sockaddr_in6*>(it->ifa_addr);
      addr = MakeInetAddressBytes(state, reinterpret_cast<const uint8_t*>(&sin6->sin6_addr), 16,
                                  nullptr, static_cast<int32_t>(sin6->sin6_scope_id), ok);
    }
    if (!*ok) {
      ::freeifaddrs(list);
      return out;
    }
    IfaceInfo* info = nullptr;
    for (auto& e : out) {
      if (e.name == it->ifa_name) {
        info = &e;
        break;
      }
    }
    if (info == nullptr) {
      IfaceInfo created;
      created.name = it->ifa_name;
      created.index = static_cast<int>(::if_nametoindex(it->ifa_name));
      out.push_back(std::move(created));
      info = &out.back();
    }
    info->addrs.push_back(addr);
  }
  ::freeifaddrs(list);
  return out;
}

VObject* MakeNetworkInterfaceObject(VMState& state, const IfaceInfo& info, bool* ok) {
  VClass* ni = state.linker->FindClass("Ljava/net/NetworkInterface;");
  if (ni == nullptr || !CheckClassDefinition(state, ni) ||
      !state.linker->InitializeClass(ni, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", "NetworkInterface missing");
    }
    *ok = false;
    return nullptr;
  }
  VObject* obj = state.linker->AllocObject(ni);
  VClass* ia_cls = state.linker->FindClass("[Ljava/net/InetAddress;");
  VArray* arr = ia_cls != nullptr ? state.linker->AllocArray(
                                        ia_cls, static_cast<int32_t>(info.addrs.size()))
                                  : nullptr;
  if (obj == nullptr || arr == nullptr) {
    ThrowOOM(state, "NetworkInterface");
    *ok = false;
    return nullptr;
  }
  for (size_t i = 0; i < info.addrs.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i),
              reinterpret_cast<ObjHeader*>(info.addrs[i]));
  }
  std::u16string u16;
  for (char c : info.name) u16.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
  VString* name = state.linker->AllocString(u16);
  VMethod probe;
  probe.name = "<init>";
  probe.return_type = "V";
  probe.param_types = {"Ljava/lang/String;", "I", "[Ljava/net/InetAddress;"};
  VMethod* ctor = ni->FindInterfaceImpl(&probe);
  if (ctor == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "NetworkInterface ctor missing");
    *ok = false;
    return nullptr;
  }
  Value args[4] = {};
  args[0].ref = obj;
  args[1].ref = name;
  args[2].i = info.index;
  args[3].ref = arr;
  Value ignored{};
  if (!InvokeMethod(state, ctor, args, 4, &ignored)) {
    *ok = false;
    return nullptr;
  }
  *ok = true;
  return obj;
}

unsigned IffFlagsForName(const std::string& name) {
  unsigned flags = 0;
  struct ifaddrs* list = nullptr;
  if (::getifaddrs(&list) == 0) {
    for (struct ifaddrs* it = list; it != nullptr; it = it->ifa_next) {
      if (it->ifa_name != nullptr && name == it->ifa_name) {
        flags = it->ifa_flags;
        break;
      }
    }
    ::freeifaddrs(list);
  }
  return flags;
}
}  // namespace

Value NetIfInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

Value NetIfGetAll(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  std::vector<IfaceInfo> infos = CollectInterfaces(state, ok);
  if (!*ok) return Value{};
  VClass* arr_cls = state.linker->FindClass("[Ljava/net/NetworkInterface;");
  VArray* arr =
      arr_cls != nullptr ? state.linker->AllocArray(arr_cls, static_cast<int32_t>(infos.size()))
                         : nullptr;
  if (arr == nullptr) {
    ThrowOOM(state, "NetworkInterface[]");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < infos.size(); i++) {
    VObject* ni = MakeNetworkInterfaceObject(state, infos[i], ok);
    if (!*ok) return Value{};
    ArrSetRef(arr, static_cast<int32_t>(i), reinterpret_cast<ObjHeader*>(ni));
  }
  Value r{};
  r.ref = arr;
  *ok = true;
  return r;
}

Value NetIfGetByName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string name = n > 0 ? Mutf8Arg(args[0]) : "";
  std::vector<IfaceInfo> infos = CollectInterfaces(state, ok);
  if (!*ok) return Value{};
  for (const auto& info : infos) {
    if (info.name == name) {
      Value r{};
      r.ref = MakeNetworkInterfaceObject(state, info, ok);
      return r;
    }
  }
  *ok = true;
  return Value{};  // null（JDK：不存在返回 null）
}

Value NetIfGetByIndex(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int index = n > 0 ? args[0].i : 0;
  std::vector<IfaceInfo> infos = CollectInterfaces(state, ok);
  if (!*ok) return Value{};
  for (const auto& info : infos) {
    if (info.index == index) {
      Value r{};
      r.ref = MakeNetworkInterfaceObject(state, info, ok);
      return r;
    }
  }
  *ok = true;
  return Value{};
}

Value NetIfGetByInetAddress(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)self;
  VObject* ia = n > 0 ? static_cast<VObject*>(args[0].ref) : nullptr;
  if (ia == nullptr) {
    *ok = true;
    return Value{};
  }
  std::vector<uint8_t> want;
  int32_t scope = 0;
  if (!InetAddrBytes(state, ia, &want, &scope, ok)) return Value{};
  std::vector<IfaceInfo> infos = CollectInterfaces(state, ok);
  if (!*ok) return Value{};
  for (const auto& info : infos) {
    for (VObject* addr : info.addrs) {
      std::vector<uint8_t> have;
      int32_t have_scope = 0;
      if (!InetAddrBytes(state, addr, &have, &have_scope, ok)) return Value{};
      if (have == want) {
        Value r{};
        r.ref = MakeNetworkInterfaceObject(state, info, ok);
        return r;
      }
    }
  }
  *ok = true;
  return Value{};
}

namespace {
Value NetIfFlagProbe(VMState& state, const Value* args, size_t n, unsigned flag, bool negate,
                     bool* ok) {
  (void)state;
  std::string name = n > 0 ? Mutf8Arg(args[0]) : "";
  unsigned flags = IffFlagsForName(name);
  bool set = (flags & flag) != 0;
  Value r{};
  r.i = (negate ? !set : set) ? 1 : 0;
  *ok = true;
  return r;
}
}  // namespace

Value NetIfIsUp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  return NetIfFlagProbe(state, args, n, IFF_UP, false, ok);
}
Value NetIfIsLoopback(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  return NetIfFlagProbe(state, args, n, IFF_LOOPBACK, false, ok);
}
Value NetIfSupportsMulticast(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  return NetIfFlagProbe(state, args, n, IFF_MULTICAST, false, ok);
}
Value NetIfIsP2P(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  return NetIfFlagProbe(state, args, n, IFF_POINTOPOINT, false, ok);
}

Value NetIfGetMTU(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  std::string name = n > 0 ? Mutf8Arg(args[0]) : "";
  int mtu = 1500;
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd >= 0) {
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFMTU, &ifr) == 0) mtu = ifr.ifr_mtu;
    ::close(fd);
  }
  Value r{};
  r.i = mtu;
  *ok = true;
  return r;
}

Value NetIfGetMacAddr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};  // null：无硬件地址面（Java getHardwareAddress 容忍）
}

// =====================================================================
// NIO 通道/选择器 native 面（sun.nio.ch.ChannelIO；netty NIO 传输面）
// 通道 = 宿主 fd（非阻塞语义按 fd 标志）；选择器 = poll(fds) 就绪判定 +
// 自唤醒管道。地址面复用 InetAddress/sockaddr 既有缝（零特判）。
// =====================================================================
namespace {
bool NioTraceEnabled() {
  static int enabled = -1;
  if (enabled < 0) enabled = std::getenv("DEXVM_TRACE_NIO") != nullptr ? 1 : 0;
  return enabled == 1;
}

void ChannelSetNonBlocking(int fd, bool nonblocking) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) return;
  if (nonblocking) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~O_NONBLOCK;
  }
  ::fcntl(fd, F_SETFL, flags);
}

void ChannelNoSigPipe(int fd) {
#ifdef SO_NOSIGPIPE
  int on = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
  (void)fd;
#endif
}

bool SocketAddressToSockaddr(VMState& state, VObject* isa, sockaddr_storage* ss, socklen_t* slen,
                             bool* ok) {
  Value addr{};
  if (!InvokeVirtualNamed(state, isa, "getAddress", {}, "Ljava/net/InetAddress;", nullptr, 0,
                          &addr)) {
    *ok = false;
    return false;
  }
  if (addr.ref == nullptr) {
    state.ThrowNew("Ljava/net/UnknownHostException;", "unresolved socket address");
    *ok = false;
    return false;
  }
  Value port{};
  if (!InvokeVirtualNamed(state, isa, "getPort", {}, "I", nullptr, 0, &port)) {
    *ok = false;
    return false;
  }
  return InetToSockaddr(state, static_cast<VObject*>(addr.ref), port.i, ss, slen, ok);
}

VObject* MakeInetSocketAddress(VMState& state, const sockaddr_storage& ss, bool* ok) {
  VObject* ia = SockaddrToInetAddress(state, ss, ok);
  if (!*ok) return nullptr;
  uint16_t port = 0;
  if (ss.ss_family == AF_INET) {
    sockaddr_in sin{};
    std::memcpy(&sin, &ss, sizeof(sin));
    port = ntohs(sin.sin_port);
  } else if (ss.ss_family == AF_INET6) {
    sockaddr_in6 sin6{};
    std::memcpy(&sin6, &ss, sizeof(sin6));
    port = ntohs(sin6.sin6_port);
  }
  VClass* cls = state.linker->FindClass("Ljava/net/InetSocketAddress;");
  if (cls == nullptr || !CheckClassDefinition(state, cls) ||
      !state.linker->InitializeClass(cls, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", "InetSocketAddress missing");
    }
    *ok = false;
    return nullptr;
  }
  VObject* obj = state.linker->AllocObject(cls);
  if (obj == nullptr) {
    ThrowOOM(state, "InetSocketAddress");
    *ok = false;
    return nullptr;
  }
  VMethod probe;
  probe.name = "<init>";
  probe.return_type = "V";
  probe.param_types = {"Ljava/net/InetAddress;", "I"};
  VMethod* ctor = cls->FindInterfaceImpl(&probe);
  if (ctor == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "InetSocketAddress ctor missing");
    *ok = false;
    return nullptr;
  }
  Value args[3] = {};
  args[0].ref = obj;
  args[1].ref = reinterpret_cast<ObjHeader*>(ia);
  args[2].i = port;
  Value ignored{};
  if (!InvokeMethod(state, ctor, args, 3, &ignored)) {
    *ok = false;
    return nullptr;
  }
  *ok = true;
  return obj;
}

// 直接缓冲区地址（lib java.nio.Buffer.address；非直接缓冲区返回 nullptr）。
uint8_t* DirectBufferBase(VObject* buf) {
  if (buf == nullptr || buf->klass == nullptr) return nullptr;
  VField* f = FindInstField(buf->klass, "address");
  if (f == nullptr || f->type_descriptor != "J") return nullptr;
  int64_t addr = 0;
  std::memcpy(&addr, buf->field_data() + f->offset, sizeof(addr));
  return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(addr));
}

int ChannelRecv(int fd, void* dst, size_t len) {
  ssize_t n;
  do {
    n = ::recv(fd, dst, len, 0);
  } while (n < 0 && errno == EINTR);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;  // 非阻塞：暂无数据
    return -2;                                             // 其它错误（调用方抛）
  }
  return static_cast<int>(n);
}

int ChannelSend(int fd, const void* src, size_t len) {
  int flags = 0;
#ifdef MSG_NOSIGNAL
  flags = MSG_NOSIGNAL;
#endif
  ssize_t n;
  do {
    n = ::send(fd, src, len, flags);
  } while (n < 0 && errno == EINTR);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -2;
  }
  return static_cast<int>(n);
}
}  // namespace

Value ChannelOpenStreamSocket(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    state.ThrowNew("Ljava/io/IOException;", std::string("socket: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  ChannelNoSigPipe(fd);
  Value r{};
  r.i = fd;
  *ok = true;
  return r;
}

Value ChannelBind(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  VObject* isa = n > 1 ? static_cast<VObject*>(args[1].ref) : nullptr;
  if (fd < 0 || isa == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "channel bind args");
    *ok = false;
    return Value{};
  }
  sockaddr_storage ss{};
  socklen_t slen = 0;
  if (!SocketAddressToSockaddr(state, isa, &ss, &slen, ok)) return Value{};
  std::string adapt_err;
  if (!AdaptSockaddrForSocket(fd, &ss, &slen, &adapt_err)) {
    state.ThrowNew("Ljava/net/SocketException;", "bind address family: " + adapt_err);
    *ok = false;
    return Value{};
  }
  if (::bind(fd, reinterpret_cast<sockaddr*>(&ss), slen) != 0) {
    state.ThrowNew("Ljava/net/SocketException;", std::string("bind: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  *ok = true;
  return Value{};
}

Value ChannelListen(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  int backlog = n > 1 && args[1].i > 0 ? args[1].i : 50;
  if (fd < 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "channel listen fd");
    *ok = false;
    return Value{};
  }
  if (::listen(fd, backlog) != 0) {
    state.ThrowNew("Ljava/net/SocketException;", std::string("listen: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  *ok = true;
  return Value{};
}

Value ChannelAccept(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  int newfd;
  do {
    newfd = ::accept(fd, nullptr, nullptr);
  } while (newfd < 0 && errno == EINTR);
  Value r{};
  if (newfd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      r.i = -1;  // 无就绪连接（非阻塞）
      *ok = true;
      return r;
    }
    state.ThrowNew("Ljava/io/IOException;", std::string("accept: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  ChannelNoSigPipe(newfd);
  if (NioTraceEnabled()) std::fprintf(stderr, "[nio] accept fd=%d -> %d\n", fd, newfd);
  r.i = newfd;
  *ok = true;
  return r;
}

Value ChannelConnect(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  VObject* isa = n > 1 ? static_cast<VObject*>(args[1].ref) : nullptr;
  if (fd < 0 || isa == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "channel connect args");
    *ok = false;
    return Value{};
  }
  sockaddr_storage ss{};
  socklen_t slen = 0;
  if (!SocketAddressToSockaddr(state, isa, &ss, &slen, ok)) return Value{};
  std::string adapt_err;
  if (!AdaptSockaddrForSocket(fd, &ss, &slen, &adapt_err)) {
    state.ThrowNew("Ljava/net/SocketException;", "connect address family: " + adapt_err);
    *ok = false;
    return Value{};
  }
  int rc;
  do {
    rc = ::connect(fd, reinterpret_cast<sockaddr*>(&ss), slen);
  } while (rc < 0 && errno == EINTR);
  Value r{};
  if (rc == 0) {
    r.i = 1;  // 已连接
    *ok = true;
    return r;
  }
  if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
    r.i = 0;  // 连接挂起（selector OP_CONNECT）
    *ok = true;
    return r;
  }
  state.ThrowNew("Ljava/net/ConnectException;", std::string("connect: ") + std::strerror(errno));
  *ok = false;
  return Value{};
}

Value ChannelFinishConnect(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  int err = 0;
  socklen_t len = sizeof(err);
  Value r{};
  if (fd < 0 || ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
    state.ThrowNew("Ljava/io/IOException;", "finishConnect: socket error");
    *ok = false;
    return Value{};
  }
  if (err == 0) {
    r.i = 1;
    *ok = true;
    return r;
  }
  if (err == EINPROGRESS || err == EALREADY) {
    r.i = 0;
    *ok = true;
    return r;
  }
  state.ThrowNew("Ljava/net/ConnectException;", std::string("finishConnect: ") +
                                                   std::strerror(err));
  *ok = false;
  return Value{};
}

Value ChannelReadDirect(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  VObject* buf = n > 1 ? static_cast<VObject*>(args[1].ref) : nullptr;
  int pos = n > 2 ? args[2].i : 0;
  int len = n > 3 ? args[3].i : 0;
  uint8_t* base = DirectBufferBase(buf);
  if (fd < 0 || base == nullptr || pos < 0 || len <= 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "readDirect args");
    *ok = false;
    return Value{};
  }
  int got = ChannelRecv(fd, base + pos, static_cast<size_t>(len));
  if (NioTraceEnabled()) std::fprintf(stderr, "[nio] readD fd=%d pos=%d len=%d -> %d\n", fd, pos, len, got);
  if (got == -2) {
    state.ThrowNew("Ljava/io/IOException;", std::string("read: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = got;  // -1 EOF / 0 would-block / >0 bytes
  *ok = true;
  return r;
}

Value ChannelReadHeap(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  VArray* arr = n > 1 ? static_cast<VArray*>(args[1].ref) : nullptr;
  int off = n > 2 ? args[2].i : 0;
  int len = n > 3 ? args[3].i : 0;
  if (fd < 0 || arr == nullptr || off < 0 || len <= 0 || off + len > arr->length) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "readHeap args");
    *ok = false;
    return Value{};
  }
  std::vector<uint8_t> tmp(static_cast<size_t>(len));
  int got = ChannelRecv(fd, tmp.data(), static_cast<size_t>(len));
  if (got > 0) ArrSetBytes(arr, off, got, tmp.data());
  if (NioTraceEnabled()) std::fprintf(stderr, "[nio] readH fd=%d off=%d len=%d -> %d\n", fd, off, len, got);
  if (got == -2) {
    state.ThrowNew("Ljava/io/IOException;", std::string("read: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = got;
  *ok = true;
  return r;
}

Value ChannelWriteDirect(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  VObject* buf = n > 1 ? static_cast<VObject*>(args[1].ref) : nullptr;
  int pos = n > 2 ? args[2].i : 0;
  int len = n > 3 ? args[3].i : 0;
  uint8_t* base = DirectBufferBase(buf);
  if (fd < 0 || base == nullptr || pos < 0 || len <= 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "writeDirect args");
    *ok = false;
    return Value{};
  }
  int put = ChannelSend(fd, base + pos, static_cast<size_t>(len));
  if (NioTraceEnabled()) std::fprintf(stderr, "[nio] writeD fd=%d pos=%d len=%d -> %d\n", fd, pos, len, put);
  if (put == -2) {
    state.ThrowNew("Ljava/io/IOException;", std::string("write: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = put;  // 0 = would-block
  *ok = true;
  return r;
}

Value ChannelWriteHeap(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  VArray* arr = n > 1 ? static_cast<VArray*>(args[1].ref) : nullptr;
  int off = n > 2 ? args[2].i : 0;
  int len = n > 3 ? args[3].i : 0;
  if (fd < 0 || arr == nullptr || off < 0 || len <= 0 || off + len > arr->length) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "writeHeap args");
    *ok = false;
    return Value{};
  }
  std::vector<uint8_t> tmp(static_cast<size_t>(len));
  ArrGetBytes(arr, off, len, tmp.data());
  int put = ChannelSend(fd, tmp.data(), static_cast<size_t>(len));
  if (NioTraceEnabled()) std::fprintf(stderr, "[nio] writeH fd=%d off=%d len=%d -> %d\n", fd, off, len, put);
  if (put == -2) {
    state.ThrowNew("Ljava/io/IOException;", std::string("write: ") + std::strerror(errno));
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = put;
  *ok = true;
  return r;
}

Value ChannelCloseFd(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  if (fd >= 0) ::close(fd);
  *ok = true;
  return Value{};
}

Value ChannelSetBlocking(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  bool block = n > 1 && args[1].i != 0;  // Java configureBlocking(block)
  if (fd >= 0) ChannelSetNonBlocking(fd, !block);
  *ok = true;
  return Value{};
}

Value ChannelLocalAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  sockaddr_storage ss{};
  socklen_t len = sizeof(ss);
  if (fd < 0 || ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len) != 0) {
    state.ThrowNew("Ljava/io/IOException;", "getLocalAddress");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = MakeInetSocketAddress(state, ss, ok);
  return r;
}

Value ChannelRemoteAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  sockaddr_storage ss{};
  socklen_t len = sizeof(ss);
  if (fd < 0 || ::getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &len) != 0) {
    *ok = true;
    return Value{};  // 未连接：null（JDK getRemoteAddress 对未连接返回 null）
  }
  Value r{};
  r.ref = MakeInetSocketAddress(state, ss, ok);
  return r;
}

Value ChannelShutdownInput(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  if (fd >= 0) ::shutdown(fd, SHUT_RD);
  *ok = true;
  return Value{};
}

Value ChannelShutdownOutput(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state;
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  if (fd >= 0) ::shutdown(fd, SHUT_WR);
  *ok = true;
  return Value{};
}

Value ChannelSetSockOpt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  int value = (n > 1 && args[1].i != 0) ? 1 : 0;
  // args[2] = option id（0=TCP_NODELAY 1=SO_KEEPALIVE 2=SO_REUSEADDR 3=SO_RCVBUF 4=SO_SNDBUF）
  int opt = n > 2 ? args[2].i : 0;
  if (fd >= 0) {
    if (opt == 0) {
      ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
    } else if (opt == 1) {
      ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &value, sizeof(value));
    } else if (opt == 2) {
      ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
    } else if (opt == 3) {
      ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));
    } else if (opt == 4) {
      ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value));
    }
  }
  *ok = true;
  return Value{};
}

namespace {
// select 协程化：等待中的 guest task 登记（read fd -> task），wakeup 直醒；
// 常规就绪变化由切片超时（≤kSelectSliceMs）兜底，避免单 carrier 被 poll 独占。
constexpr int kSelectSliceMs = 20;
std::unordered_map<int, int> g_wake_read_by_write;
std::unordered_map<int, void*> g_wait_task_by_read;
}  // namespace

Value ChannelNewWakeupPipe(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  int fds[2] = {-1, -1};
  if (::pipe(fds) != 0) {
    state.ThrowNew("Ljava/io/IOException;", "wakeup pipe");
    *ok = false;
    return Value{};
  }
  ChannelSetNonBlocking(fds[0], true);
  ChannelSetNonBlocking(fds[1], true);
  g_wake_read_by_write[fds[1]] = fds[0];
  VArray* arr = state.linker->AllocArray(state.linker->FindClass("[I"), 2);
  if (arr == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "wakeup pipe array");
    *ok = false;
    return Value{};
  }
  int tmp = fds[0];
  std::memcpy(arr->data(), &tmp, 4);
  tmp = fds[1];
  std::memcpy(arr->data() + 8, &tmp, 4);
  Value r{};
  r.ref = arr;
  *ok = true;
  return r;
}

Value ChannelWakeupSignal(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  int fd = n > 0 ? args[0].i : -1;
  if (fd >= 0) {
    const char b = 1;
    ssize_t rc = ::write(fd, &b, 1);
    (void)rc;
    // 等待中的 select 直接唤醒（不必等切片超时）
    auto it = g_wake_read_by_write.find(fd);
    if (it != g_wake_read_by_write.end()) {
      auto w = g_wait_task_by_read.find(it->second);
      const GuestSchedOps* ops = GuestSched();
      if (w != g_wait_task_by_read.end() && w->second != nullptr && ops != nullptr) {
        ops->wake(w->second, kGuestWaitJoin);
      }
    }
  }
  *ok = true;
  return Value{};
}

Value ChannelSelect(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)state;
  VArray* fds = n > 0 ? static_cast<VArray*>(args[0].ref) : nullptr;
  VArray* ops = n > 1 ? static_cast<VArray*>(args[1].ref) : nullptr;
  int wake_fd = n > 2 ? args[2].i : -1;
  int64_t timeout_ms = n > 3 ? args[3].l : -1;
  Value r{};
  if (fds == nullptr || ops == nullptr || fds->length != ops->length) {
    *ok = false;
    return r;
  }
  int count = fds->length;
  std::vector<struct pollfd> pfds;
  std::vector<int> key_ops;
  pfds.reserve(static_cast<size_t>(count) + 1);
  key_ops.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; i++) {
    int fd = 0;
    int op = 0;
    std::memcpy(&fd, fds->data() + static_cast<size_t>(i) * 8, 4);
    std::memcpy(&op, ops->data() + static_cast<size_t>(i) * 8, 4);
    if (fd < 0) {
      // 占位（无效 key）：ready=0，不参与 poll
      struct pollfd ph{};
      ph.fd = -1;
      pfds.push_back(ph);
      key_ops.push_back(0);
      continue;
    }
    struct pollfd p{};
    p.fd = fd;
    p.events = 0;
    if ((op & 0x1) != 0 || (op & 0x10) != 0) p.events |= POLLIN;   // READ/ACCEPT
    if ((op & 0x4) != 0 || (op & 0x8) != 0) p.events |= POLLOUT;  // WRITE/CONNECT
    pfds.push_back(p);
    key_ops.push_back(op);
  }
  struct pollfd wake_pfd{};
  int wake_index = -1;
  if (wake_fd >= 0) {
    wake_pfd.fd = wake_fd;
    wake_pfd.events = POLLIN;
    wake_index = static_cast<int>(pfds.size());
    pfds.push_back(wake_pfd);
  }
  // 协程化 select：poll 只做切片（≤kSelectSliceMs），无就绪时经调度器等待
  // 切片——单 carrier 下其他 GuestTask（定时器/业务线程）得以推进；wakeup
  // 立即唤醒等待任务重查就绪（native ChannelWakeupSignal）。
  auto start = std::chrono::steady_clock::now();
  int rc = 0;
  for (;;) {
    do {
      rc = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), kSelectSliceMs);
    } while (rc < 0 && errno == EINTR);
    if (rc != 0) break;
    if (timeout_ms == 0) break;  // selectNow：非阻塞一次
    long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
    long long remaining = timeout_ms < 0 ? -1 : timeout_ms - elapsed;
    if (timeout_ms >= 0 && remaining <= 0) break;
    const GuestSchedOps* ops = GuestSched();
    if (ops != nullptr) {
      void* me = ops->current_task();
      if (me != nullptr && wake_fd >= 0) g_wait_task_by_read[wake_fd] = me;
      ops->wait(kGuestWaitJoin, ops->now_ms() + kSelectSliceMs,
                [](void*) -> bool { return false; }, nullptr);
      if (me != nullptr && wake_fd >= 0) g_wait_task_by_read.erase(wake_fd);
    } else {
      struct timespec ts{};
      ts.tv_sec = 0;
      ts.tv_nsec = kSelectSliceMs * 1000000L;
      ::nanosleep(&ts, nullptr);
    }
  }
  if (wake_index >= 0 && (pfds[static_cast<size_t>(wake_index)].revents & POLLIN) != 0) {
    char drain[64];
    while (::read(wake_fd, drain, sizeof(drain)) > 0) {
    }
  }
  int ready = 0;
  for (int i = 0; i < count; i++) {
    int op = key_ops[static_cast<size_t>(i)];
    struct pollfd& p = pfds[static_cast<size_t>(i)];
    int out = 0;
    if (p.fd >= 0) {
      if ((p.revents & (POLLIN | POLLHUP | POLLERR)) != 0) out |= (op & 0x1) | (op & 0x10);
      if ((p.revents & POLLOUT) != 0) out |= (op & 0x4) | (op & 0x8);
      if (out != 0) ready++;
    }
    std::memcpy(ops->data() + static_cast<size_t>(i) * 8, &out, 4);
  }
  if (NioTraceEnabled() && ready > 0) {
    std::fprintf(stderr, "[nio] select ready=%d", ready);
    for (int i = 0; i < count; i++) {
      int fd = 0, op = 0;
      std::memcpy(&fd, fds->data() + static_cast<size_t>(i) * 8, 4);
      std::memcpy(&op, ops->data() + static_cast<size_t>(i) * 8, 4);
      if (op != 0) std::fprintf(stderr, " (fd=%d op=%d)", fd, op);
    }
    std::fprintf(stderr, "\n");
  }
  r.i = ready;
  *ok = true;
  return r;
}

// ---- java.util.zip.Inflater native 面（zlib 直通；gzip 响应解码）----
// OkHttp 透明 gzip / GZIPInputStream 依赖；此前 native 槽未绑定，响应体
// 一律卡在 Inflater 调用。guest byte[] 槽宽 8 字节，需显式逐字节搬运。
namespace {

struct InflaterHandle {
  z_stream zs;
  bool inited = false;
  bool finished = false;
  bool needs_dict = false;
  int remaining = 0;
};

InflaterHandle* AsInflater(int64_t h) {
  return reinterpret_cast<InflaterHandle*>(static_cast<intptr_t>(h));
}

bool ReadByteArray(VArray* arr, int32_t off, int32_t len, std::vector<uint8_t>* out) {
  if (arr == nullptr || off < 0 || len < 0 || off > arr->length - len) return false;
  out->resize(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; i++) {
    uint64_t slot = 0;
    std::memcpy(&slot, arr->data() + (static_cast<size_t>(off) + i) * 8, sizeof(slot));
    (*out)[static_cast<size_t>(i)] = static_cast<uint8_t>(slot & 0xffu);
  }
  return true;
}

void WriteByteArray(const uint8_t* src, VArray* arr, int32_t off, int32_t len) {
  for (int32_t i = 0; i < len; i++) {
    uint64_t slot = src[i];
    std::memcpy(arr->data() + (static_cast<size_t>(off) + i) * 8, &slot, sizeof(slot));
  }
}

}  // namespace

Value InflaterInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  bool nowrap = n >= 1 && args[0].i != 0;
  auto* h = new (std::nothrow) InflaterHandle();
  if (h == nullptr) return Value{};
  std::memset(&h->zs, 0, sizeof(h->zs));
  if (inflateInit2(&h->zs, nowrap ? -MAX_WBITS : MAX_WBITS) != Z_OK) {
    delete h;
    return Value{};
  }
  h->inited = true;
  Value r{};
  r.l = static_cast<int64_t>(reinterpret_cast<intptr_t>(h));
  return r;
}

Value InflaterInflate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self;
  *ok = true;
  Value r{};
  r.i = -1;
  if (n < 7) return r;
  InflaterHandle* h = AsInflater(args[0].l);
  VArray* in = static_cast<VArray*>(args[1].ref);
  int32_t in_off = args[2].i;
  int32_t in_len = args[3].i;
  VArray* out = static_cast<VArray*>(args[4].ref);
  int32_t out_off = args[5].i;
  int32_t out_len = args[6].i;
  if (h == nullptr || !h->inited || out == nullptr) return r;
  // 未 setInput 时 guest 传 input==null/len==0：这是合法状态（needsInput），
  // 不是数据格式错误——旧实现直接返回 -1，导致 Inflater 抛 DataFormatException，
  // GZIPInputStream 读头即失败（OkHttp publicsuffix 资源解码中断的根因）。
  std::vector<uint8_t> inbuf;
  if (in == nullptr) {
    if (in_len != 0 || in_off != 0) return r;
  } else if (!ReadByteArray(in, in_off, in_len, &inbuf)) {
    return r;
  }
  std::vector<uint8_t> outbuf;
  if (out_len > 0) outbuf.resize(static_cast<size_t>(out_len));
  h->zs.next_in = inbuf.empty() ? nullptr : inbuf.data();
  h->zs.avail_in = static_cast<uInt>(inbuf.size());
  h->zs.next_out = outbuf.empty() ? nullptr : outbuf.data();
  h->zs.avail_out = static_cast<uInt>(outbuf.size());
  int rc = inflate(&h->zs, Z_SYNC_FLUSH);
  int made = static_cast<int>(outbuf.size() - h->zs.avail_out);
  h->remaining = static_cast<int>(h->zs.avail_in);
  if (made > 0) WriteByteArray(outbuf.data(), out, out_off, made);
  if (rc == Z_STREAM_END) {
    h->finished = true;
    r.i = made;
    return r;
  }
  if (rc == Z_NEED_DICT) {
    h->needs_dict = true;
    r.i = made;
    return r;
  }
  if (rc == Z_OK || rc == Z_BUF_ERROR) {
    r.i = made;
    return r;
  }
  r.i = rc;  // 负值 → guest Inflater 抛 DataFormatException
  return r;
}

Value InflaterSetDictionary(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  if (n < 4) return Value{};
  InflaterHandle* h = AsInflater(args[0].l);
  VArray* dict = static_cast<VArray*>(args[1].ref);
  std::vector<uint8_t> bytes;
  if (h == nullptr || !h->inited || !ReadByteArray(dict, args[2].i, args[3].i, &bytes)) {
    return Value{};
  }
  if (!bytes.empty()) {
    inflateSetDictionary(&h->zs, bytes.data(), static_cast<uInt>(bytes.size()));
  }
  return Value{};
}

Value InflaterRemaining(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  InflaterHandle* h = n >= 1 ? AsInflater(args[0].l) : nullptr;
  r.i = h != nullptr ? h->remaining : 0;
  return r;
}

Value InflaterNeedsDictionary(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  InflaterHandle* h = n >= 1 ? AsInflater(args[0].l) : nullptr;
  r.i = h != nullptr && h->needs_dict ? 1 : 0;
  return r;
}

Value InflaterFinished(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  InflaterHandle* h = n >= 1 ? AsInflater(args[0].l) : nullptr;
  r.i = h != nullptr && h->finished ? 1 : 0;
  return r;
}

Value InflaterAdler(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  InflaterHandle* h = n >= 1 ? AsInflater(args[0].l) : nullptr;
  r.i = h != nullptr ? static_cast<int32_t>(h->zs.adler) : 0;
  return r;
}

Value InflaterReset(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  InflaterHandle* h = n >= 1 ? AsInflater(args[0].l) : nullptr;
  if (h != nullptr && h->inited) {
    inflateReset(&h->zs);
    h->finished = false;
    h->needs_dict = false;
    h->remaining = 0;
  }
  return Value{};
}

Value InflaterEnd(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  InflaterHandle* h = n >= 1 ? AsInflater(args[0].l) : nullptr;
  if (h != nullptr) {
    if (h->inited) inflateEnd(&h->zs);
    delete h;
  }
  return Value{};
}

}  // namespace dexvm::runtime
