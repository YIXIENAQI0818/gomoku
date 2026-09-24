#include <gomoku/listener.h>
#include <gomoku/connection_manager.h>
#include <gomoku/session.h>

#include <spdlog/spdlog.h>

namespace gomoku {

Listener::Listener(asio::io_context& io, tcp::endpoint endpoint, ConnectionManager& cm)
    : io_(io), acceptor_(io, endpoint), cm_(cm) {
    acceptor_.set_option(asio::socket_base::reuse_address(true));
}

void Listener::run() {
    do_accept();
}

void Listener::do_accept() {
    acceptor_.async_accept(
        asio::make_strand(io_),
        beast::bind_front_handler(&Listener::on_accept, shared_from_this()));
}

void Listener::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        spdlog::error("接受连接失败: {}", ec.message());
    } else {
        // 每个连接一个 Session,并传入共享的 ConnectionManager。
        std::make_shared<Session>(std::move(socket), cm_)->run();
    }
    do_accept();  // 继续接受下一个连接
}

}  // namespace gomoku
