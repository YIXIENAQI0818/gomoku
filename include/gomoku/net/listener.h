#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>

#include <functional>
#include <memory>

namespace gomoku {

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

// 监听端口,接受新连接,并把每个新 socket 交给外部回调处理。
// 只依赖 std::function,不持有 ConnectionManager/MessageRouter,避免依赖透传;
// 「如何创建 Session」由 Server 在注入回调时决定。
class Listener : public std::enable_shared_from_this<Listener> {
public:
    // 拿到新连接 socket 后的回调(由 Server 注入,负责创建并启动 Session)。
    using OnConnect = std::function<void(tcp::socket)>;

    Listener(asio::io_context& io, tcp::endpoint endpoint, OnConnect on_connect);

    void run();   // 开始监听接受连接
    void stop();  // 停止监听:不再 accept

private:
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);

    asio::io_context& _io;
    tcp::acceptor _acceptor;
    OnConnect _on_connect;
    bool _stopped = false;
};

}  // namespace gomoku
