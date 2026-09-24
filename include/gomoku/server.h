#pragma once

#include <boost/asio.hpp>
#include <gomoku/connection_manager.h>
#include <gomoku/message_router.h>

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

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

private:
    ConnectionManager _cm;                  // 先构造
    MessageRouter _router;                  // 先构造
    std::shared_ptr<Listener> _listener;    // 后构造(回调捕获 this 引用 _cm/_router)
};

}  // namespace gomoku
