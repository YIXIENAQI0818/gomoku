#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <cstddef>
#include <memory>

namespace gomoku {

class ConnectionManager;

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

// 代表一个已连接的客户端(一个 WebSocket 会话)。
// 继承 enable_shared_from_this:异步回调里用 shared_from_this() 持有自身,
// 保证在异步操作期间对象不被析构。
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, ConnectionManager& cm);

    // 开始:先完成 WebSocket 握手。
    void run();

    // 主动关闭连接(供 ConnectionManager::stop_all 批量关闭)。
    void close();

private:
    void on_accept(beast::error_code ec);
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes);
    void on_write(beast::error_code ec, std::size_t bytes);

    // 统一收尾:从 ConnectionManager 注销自身,保证只执行一次。
    void shutdown();

    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    ConnectionManager& cm_;  // 裸引用,不持有所有权,避免与 manager 形成 shared_ptr 循环
    bool closed_ = false;
};

}  // namespace gomoku
