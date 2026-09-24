#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>

#include <memory>

namespace gomoku {

class ConnectionManager;

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

// 监听端口,接受新连接,并为每个连接创建一个 Session。
class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(asio::io_context& io, tcp::endpoint endpoint, ConnectionManager& cm);

    void run();

private:
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);

    asio::io_context& _io;
    tcp::acceptor _acceptor;
    ConnectionManager& _cm;
};

}  // namespace gomoku
