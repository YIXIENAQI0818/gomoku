#include <gomoku/session.h>
#include <gomoku/connection_manager.h>
#include <gomoku/message_router.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <utility>

// 心跳 / 超时底层参数(硬编码,备选见下):
// - handshake_timeout: 20s(备选 10s / 30s,防止恶意客户端挂起握手)
// - idle_timeout:      20s(备选 30s / 60s,空闲多久判死)
// - keep_alive_pings:  true(beast 自动发协议层 ping 探测对端)

namespace gomoku {

Session::Session(tcp::socket socket, ConnectionManager& cm, MessageRouter& router)
    : _ws(std::move(socket)), _cm(cm), _router(router) {}

void Session::run() {
    // 心跳 / 超时配置:handshake_timeout 约束握手;idle_timeout + keep_alive_pings
    // 在空闲时自动发协议层 ping,对端不回 pong 即判死(async_read 以 timeout 失败)。
    _ws.set_option(websocket::stream_base::timeout{
        std::chrono::seconds(20),  // handshake_timeout
        std::chrono::seconds(20),  // idle_timeout
        true,                       // keep_alive_pings
    });

    // 握手阶段:用 shared_from_this 让 async_accept 的回调持有本对象。
    _ws.async_accept(
        beast::bind_front_handler(&Session::on_accept, shared_from_this()));
}

void Session::on_accept(beast::error_code ec) {
    if (ec) {
        spdlog::warn("握手失败: {}", ec.message());
        return;  // 尚未 join,无需 leave
    }
    _cm.join(shared_from_this());
    spdlog::info("客户端已连接,当前连接数 {}", _cm.size());
    do_read();
}

void Session::do_read() {
    _ws.async_read(_buffer,
        beast::bind_front_handler(&Session::on_read, shared_from_this()));
}

void Session::on_read(beast::error_code ec, std::size_t bytes) {
    if (ec == websocket::error::closed) {
        // 客户端主动关闭:已收到 Close Frame,直接清理,无需再回应。
        shutdown();
        return;
    }
    if (ec) {
        // 主动关闭(close()/async_close)会取消或收尾挂起的读,属预期路径;
        // 只有仍处于 open 状态的读失败才是真正的异常。
        if (_state == State::open) {
            if (ec == beast::error::timeout) {
                spdlog::info("心跳超时,判定死连接,主动断开");
            } else {
                spdlog::warn("读取失败: {}", ec.message());
            }
        }
        shutdown();
        return;
    }

    std::string text = beast::buffers_to_string(_buffer.data());
    _buffer.consume(_buffer.size());

    // 协议解析与分发上移到协议层(Session 不碰 JSON/Message)。
    _router.handle_text(text, *this);

    // handler 可能已调用 close() 进入 closing,此时不能再启动新的读。
    if (_state == State::open) {
        do_read();
    }
}

void Session::send_text(std::string text) {
    if (_state != State::open) return;  // 关闭中/已关闭,不再发送
    _outgoing.push_back(std::move(text));
    if (!_writing) do_write();
}

void Session::do_write() {
    _writing = true;
    _ws.async_write(asio::buffer(_outgoing.front()),
        beast::bind_front_handler(&Session::on_write, shared_from_this()));
}

void Session::on_write(beast::error_code ec, std::size_t bytes) {
    if (ec) {
        // 写失败:仅 open 状态才算真错误;closing/closed 是关闭流程中的预期中断
        // (如 async_write 期间被客户端关闭),不再当作错误记录。
        _writing = false;
        if (_state == State::open) {
            spdlog::warn("写入失败: {}", ec.message());
        }
        shutdown();
        return;
    }
    _outgoing.pop_front();
    _writing = false;

    // async_write 期间,状态可能已被其他回调改动(如 on_read 收到客户端关闭 → closed),
    // 这里必须重新判断,不能假设仍在 open。
    if (_state == State::closed) {
        return;  // 连接已彻底关闭,不再做任何写操作
    }
    if (_state == State::closing) {
        // 服务器主动关闭:发完剩余消息,队列清空后再发 Close Frame。
        if (_outgoing.empty()) {
            do_close();
        } else {
            do_write();
        }
        return;
    }
    // _state == open:正常继续写队列。
    if (!_outgoing.empty()) {
        do_write();
    }
}

void Session::close() {
    if (_state != State::open) return;  // 幂等
    _state = State::closing;
    if (!_writing) do_close();  // 有挂起写则等 on_write 清空后触发
}

void Session::do_close() {
    _ws.async_close(websocket::close_code::normal,
        beast::bind_front_handler(&Session::on_close, shared_from_this()));
}

void Session::on_close(beast::error_code ec) {
    shutdown();
}

void Session::shutdown() {
    if (_state == State::closed) return;  // 幂等:只注销一次
    _state = State::closed;
    _cm.leave(shared_from_this());
    spdlog::info("客户端断开,当前连接数 {}", _cm.size());
}

}  // namespace gomoku
