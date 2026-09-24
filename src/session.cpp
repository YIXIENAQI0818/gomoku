#include <gomoku/session.h>
#include <gomoku/connection_manager.h>
#include <gomoku/message_router.h>

#include <spdlog/spdlog.h>

#include <utility>

namespace gomoku {

Session::Session(tcp::socket socket, ConnectionManager& cm, MessageRouter& router)
    : _ws(std::move(socket)), _cm(cm), _router(router) {}

void Session::run() {
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
        // 只有仍处于 open 状态的读失败才是真正的异常(对端异常断开等)。
        if (_state == State::open) {
            spdlog::warn("读取失败: {}", ec.message());
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
        spdlog::warn("写入失败: {}", ec.message());
        shutdown();
        return;
    }
    _outgoing.pop_front();
    _writing = false;
    if (!_outgoing.empty()) {
        do_write();  // 继续写队列
    } else if (_state == State::closing) {
        do_close();  // 队列清空且待关闭 → 发 Close Frame
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
