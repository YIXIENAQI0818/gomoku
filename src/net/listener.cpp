#include <gomoku/net/listener.h>

#include <spdlog/spdlog.h>

#include <utility>

namespace gomoku {

Listener::Listener(asio::io_context& io, tcp::endpoint endpoint, OnConnect on_connect)
    : _io(io), _acceptor(io, endpoint), _on_connect(std::move(on_connect)) {
    _acceptor.set_option(asio::socket_base::reuse_address(true));
}

void Listener::run() {
    do_accept();
}

void Listener::stop() {
    _stopped = true;
    _acceptor.cancel();  // 让挂起的 async_accept 以 aborted 返回
    _acceptor.close();
}

void Listener::do_accept() {
    _acceptor.async_accept(
        asio::make_strand(_io),
        beast::bind_front_handler(&Listener::on_accept, shared_from_this()));
}

void Listener::on_accept(beast::error_code ec, tcp::socket socket) {
    // stop() 会先置 _stopped=true 再 cancel acceptor,cancel 让本回调以
    // operation_aborted 返回;此处先判 _stopped 即可静默收尾,无需再判 ec。
    if (_stopped) return;
    if (ec) {
        spdlog::error("接受连接失败: {}", ec.message());
    } else {
        _on_connect(std::move(socket));  // 交给 Server 创建 Session
    }
    do_accept();
}

}  // namespace gomoku
