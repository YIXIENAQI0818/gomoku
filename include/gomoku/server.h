#pragma once

#include <boost/asio.hpp>
#include <gomoku/net/connection_manager.h>
#include <gomoku/protocol/message_router.h>
#include <gomoku/data/db_pool.h>
#include <gomoku/data/cache.h>

#include <memory>

namespace gomoku {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class Listener;

// 应用生命周期中心:组织连接层(ConnectionManager)、协议层(MessageRouter)、
// 监听(Listener),负责启动与优雅停机。依赖在此汇聚,不再向下透传。
// 单进程单实例(由 main 持有),普通类,禁止拷贝。
class Server {
public:
    Server(asio::io_context& io, tcp::endpoint endpoint);

    void run();   // 开始监听接受连接
    void stop();  // 优雅停机:停止监听 + 关闭所有连接

    MessageRouter& router() noexcept { return _router; }
    ConnectionManager& connections() noexcept { return _cm; }
    DBPool& db() noexcept { return _db; }
    Cache& cache() noexcept { return _cache; }

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

private:
    ConnectionManager _cm;                  // 先构造
    MessageRouter _router;                  // 先构造
    DBPool _db;                             // 数据层:MySQL(线程池)
    Cache _cache;                           // 数据层:Redis(单工作线程)
    std::shared_ptr<Listener> _listener;    // 后构造(回调捕获 this 引用 _cm/_router)
};

}  // namespace gomoku
