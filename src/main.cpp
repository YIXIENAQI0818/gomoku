#include <gomoku/message.h>
#include <gomoku/request_context.h>
#include <gomoku/server.h>
#include <gomoku/session.h>

#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

int main(int argc, char* argv[]) {
    int port = argc > 1 ? std::stoi(argv[1]) : 8080;

    try {
        asio::io_context io{1};  // 单线程事件循环
        gomoku::Server server(
            io, tcp::endpoint(tcp::v4(), static_cast<unsigned short>(port)));

        // 注册业务 handler(阶段 2 起会拆到各服务文件)。
        server.router().register_handler("echo",
            [](const gomoku::RequestContext& ctx) {
                ctx.session.send_text(gomoku::serialize_message("echo", ctx.data));
            });
        server.router().register_handler("ping",
            [](const gomoku::RequestContext& ctx) {
                ctx.session.send_text(gomoku::serialize_message("pong", nlohmann::json::object()));
            });

        // 优雅停机:收到 SIGINT/SIGTERM 时停止监听并关闭所有连接。
        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code& ec, int) {
            if (!ec) {
                spdlog::info("收到停机信号,开始优雅关闭");
                server.stop();
            }
        });

        server.run();
        spdlog::info("gomoku-server 启动,监听端口 {}", port);
        io.run();
    } catch (const std::exception& e) {
        spdlog::error("启动失败: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
