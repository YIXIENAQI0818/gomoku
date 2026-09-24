#include <gomoku/session.h>
#include <gomoku/connection_manager.h>

#include <spdlog/spdlog.h>

namespace gomoku {

Session::Session(tcp::socket socket, ConnectionManager& cm)
    : ws_(std::move(socket)), cm_(cm) {}

void Session::run() {
    // 握手阶段:用 shared_from_this 让 async_accept 的回调持有本对象。
    ws_.async_accept(
        beast::bind_front_handler(&Session::on_accept, shared_from_this()));
}

void Session::close() {
    // 直接关闭底层 socket,让挂起的 async_read 以错误收尾,
    // 从而进入 on_read 的收尾分支(→ shutdown → leave)。
    beast::error_code ec;
    ws_.next_layer().close(ec);
}

void Session::on_accept(beast::error_code ec) {
    if (ec) {
        spdlog::warn("握手失败: {}", ec.message());
        return;  // 尚未 join,无需 leave
    }
    // 握手成功,登记到连接管理器。
    cm_.join(shared_from_this());
    spdlog::info("客户端已连接,当前连接数 {}", cm_.size());
    do_read();
}

void Session::do_read() {
    ws_.async_read(buffer_,
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
    // echo:把收到的消息原样写回。
    ws_.text(ws_.got_text());
    ws_.async_write(buffer_.data(),
        beast::bind_front_handler(&Session::on_write, shared_from_this()));
}

void Session::on_write(beast::error_code ec, std::size_t bytes) {
    if (ec) {
        spdlog::warn("写入失败: {}", ec.message());
        shutdown();
        return;
    }
    buffer_.consume(buffer_.size());
    do_read();
}

void Session::shutdown() {
    if (closed_) return;  // 幂等:只注销一次
    closed_ = true;
    cm_.leave(shared_from_this());
    spdlog::info("客户端断开,当前连接数 {}", cm_.size());
}

}  // namespace gomoku
