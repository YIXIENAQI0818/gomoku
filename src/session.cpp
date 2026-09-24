#include <gomoku/session.h>
#include <gomoku/connection_manager.h>
#include <gomoku/message.h>

#include <spdlog/spdlog.h>

namespace gomoku {

Session::Session(tcp::socket socket, ConnectionManager& cm)
    : _ws(std::move(socket)), _cm(cm) {}

void Session::run() {
    // 握手阶段:用 shared_from_this 让 async_accept 的回调持有本对象。
    _ws.async_accept(
        beast::bind_front_handler(&Session::on_accept, shared_from_this()));
}

void Session::close() {
    // 直接关闭底层 socket,让挂起的 async_read 以错误收尾,
    // 从而进入 on_read 的收尾分支(→ shutdown → leave)。
    beast::error_code ec;
    _ws.next_layer().close(ec);
}

void Session::on_accept(beast::error_code ec) {
    if (ec) {
        spdlog::warn("握手失败: {}", ec.message());
        return;  // 尚未 join,无需 leave
    }
    // 握手成功,登记到连接管理器。
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
        shutdown();
        return;
    }
    if (ec) {
        spdlog::warn("读取失败: {}", ec.message());
        shutdown();
        return;
    }
    // 1.3:把收到的原始字节解析成结构化消息。
    std::string text = beast::buffers_to_string(_buffer.data());
    _buffer.consume(_buffer.size());

    auto msg = parse_message(text);
    if (!msg) {
        spdlog::warn("收到非法消息,忽略: {}", text);
        do_read();
        return;
    }

    spdlog::info("收到消息 type={}", msg->type);
    // 暂时回一个结构化 ack,按 type 路由留到 1.4。
    nlohmann::json data = {{"received", msg->type}};
    _out = serialize_message("echo", data);
    _ws.text(true);
    _ws.async_write(boost::asio::buffer(_out),
        beast::bind_front_handler(&Session::on_write, shared_from_this()));
}

void Session::on_write(beast::error_code ec, std::size_t bytes) {
    if (ec) {
        spdlog::warn("写入失败: {}", ec.message());
        shutdown();
        return;
    }
    _out.clear();  // 写完成,清空输出缓冲
    do_read();
}

void Session::shutdown() {
    if (_closed) return;  // 幂等:只注销一次
    _closed = true;
    _cm.leave(shared_from_this());
    spdlog::info("客户端断开,当前连接数 {}", _cm.size());
}

}  // namespace gomoku
