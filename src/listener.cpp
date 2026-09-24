#include <gomoku/listener.h>
#include <gomoku/connection_manager.h>
#include <gomoku/session.h>

#include <spdlog/spdlog.h>

namespace gomoku {

Listener::Listener(asio::io_context& io, tcp::endpoint endpoint, ConnectionManager& cm)
    : _io(io), _acceptor(io, endpoint), _cm(cm) {
    _acceptor.set_option(asio::socket_base::reuse_address(true));
}

void Listener::run() {
    do_accept();
}

void Listener::do_accept() {
    _acceptor.async_accept(
        asio::make_strand(_io),
        beast::bind_front_handler(&Listener::on_accept, shared_from_this()));
}

void Listener::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        spdlog::error("接受连接失败: {}", ec.message());
    } else {
        // 每个连接一个 Session,并传入共享的 ConnectionManager。
        std::make_shared<Session>(std::move(socket), _cm)->run();
    }
    do_accept();  // 继续接受下一个连接
}

}  // namespace gomoku
