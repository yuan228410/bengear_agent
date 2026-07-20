#pragma once

// net 内部共享辅助：避免各 .cpp 在匿名命名空间重复定义（Unity Build 合并编译单元时会冲突）。

namespace ben_gear::net {

/// 发送标志：Linux 下忽略 SIGPIPE，其余平台返回 0
inline int send_flags() noexcept {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

}  // namespace ben_gear::net
