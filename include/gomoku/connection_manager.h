#pragma once

#include <cstddef>
#include <memory>
#include <unordered_set>

namespace gomoku {

class Session;

// 连接层:跟踪所有活跃连接,负责连接的登记 / 注销 / 批量关闭。
// 单进程单实例(由 main 持有),生命周期贯穿整个进程。
class ConnectionManager {
public:
    ConnectionManager() = default;

    // 持有连接集合,拷贝语义无意义,禁止拷贝。
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

    // 新连接接入,登记到集合。由 Session 握手成功后调用。
    void join(const std::shared_ptr<Session>& session);

    // 连接关闭,从集合移除。由 Session 断开时调用。
    void leave(const std::shared_ptr<Session>& session);

    // 主动关闭所有连接。服务器停机(优雅关闭)时调用。
    void stop_all();

    // 当前活跃连接数。
    std::size_t size() const noexcept { return sessions_.size(); }

private:
    // 用 shared_ptr 持有 Session:连接存活期间由这里统一管理其生命周期。
    std::unordered_set<std::shared_ptr<Session>> sessions_;
};

}  // namespace gomoku
