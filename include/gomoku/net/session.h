#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <deque>
#include <memory>
#include <string>

namespace gomoku {

class ConnectionManager;
class MessageRouter;

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

// 代表一个已连接的客户端(一个 WebSocket 会话)。
// 职责收敛为「纯连接层」:收发消息 + 生命周期管理,不涉及 JSON/Message 解析
// (解析与分发上移到协议层 MessageRouter)。
//
// 继承 enable_shared_from_this:异步回调里用 shared_from_this() 持有自身,
// 保证在异步操作期间对象不被析构。
//
// 单线程假设:所有成员仅在 io_context 的单个线程内访问。将来引入线程池 /
// 房间广播 / 服务端主动推送时,须用 strand 串行化对 _outgoing/_writing/_ws/_state
// 的访问(见 1.4 memory 的约束记录)。
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, ConnectionManager& cm, MessageRouter& router);

    void run();                        // 开始:先完成 WebSocket 握手
    void send_text(std::string text);  // 发送一条文本消息(入写队列)
    void close();                      // 服务器主动关闭:走优雅关闭握手

private:
    void on_accept(beast::error_code ec);
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes);
    void do_write();
    void on_write(beast::error_code ec, std::size_t bytes);
    void do_close();
    void on_close(beast::error_code ec);
    void shutdown();  // 统一收尾:注销自身,幂等

    // 连接状态机:open →(close)→ closing →(on_close)→ closed
    enum class State { open, closing, closed };

    websocket::stream<tcp::socket> _ws;
    beast::flat_buffer _buffer;
    std::deque<std::string> _outgoing;  // 写队列:支持连续发送/推送
    ConnectionManager& _cm;             // 裸引用,由 Server 持有
    MessageRouter& _router;             // 裸引用,由 Server 持有
    State _state = State::open;
    bool _writing = false;              // 是否有挂起的 async_write
};

}  // namespace gomoku
