#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

// 代表一个已连接的客户端(一个 WebSocket 会话)
class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket) : ws_(std::move(socket)) {}

    // 开始:先完成 WebSocket 握手
    void run() {
        ws_.async_accept(
            beast::bind_front_handler(&Session::on_accept, shared_from_this()));
    }

private:
    void on_accept(beast::error_code ec) {
        if (ec) {
            spdlog::warn("握手失败: {}", ec.message());
            return;
        }
        spdlog::info("客户端已连接");
        do_read();
    }

    void do_read() {
        ws_.async_read(buffer_,
            beast::bind_front_handler(&Session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes) {
        if (ec == websocket::error::closed) {
            spdlog::info("客户端断开");
            return;
        }
        if (ec) {
            spdlog::warn("读取失败: {}", ec.message());
            return;
        }
        // echo:把收到的消息原样写回
        ws_.text(ws_.got_text());
        ws_.async_write(buffer_.data(),
            beast::bind_front_handler(&Session::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes) {
        if (ec) {
            spdlog::warn("写入失败: {}", ec.message());
            return;
        }
        buffer_.consume(buffer_.size());  // 清空缓冲,准备读下一条
        do_read();
    }

    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
};

// 监听端口,接受新连接并为每个连接创建一个 Session
class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(asio::io_context& io, tcp::endpoint endpoint)
        : io_(io), acceptor_(io, endpoint) {
        acceptor_.set_option(asio::socket_base::reuse_address(true));
    }

    void run() { do_accept(); }

private:
    void do_accept() {
        acceptor_.async_accept(
            asio::make_strand(io_),
            beast::bind_front_handler(&Listener::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            spdlog::error("接受连接失败: {}", ec.message());
        }
        else {
            std::make_shared<Session>(std::move(socket))->run();
        }

        do_accept();
    }

    asio::io_context& io_;
    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    int port = argc > 1 ? std::stoi(argv[1]) : 8080;

    try {
        asio::io_context io{1};  // 单线程事件循环
        auto listener = std::make_shared<Listener>(
            io, tcp::endpoint(tcp::v4(), static_cast<unsigned short>(port)));
        listener->run();
        spdlog::info("gomoku-server 启动,监听端口 {}", port);
        io.run();
    } catch (const std::exception& e) {
        spdlog::error("启动失败: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
