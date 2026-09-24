#include <gomoku/connection_manager.h>
#include <gomoku/listener.h>

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

int main(int argc, char* argv[]) {
    int port = argc > 1 ? std::stoi(argv[1]) : 8080;

    try {
        asio::io_context io{1};        // 单线程事件循环
        gomoku::ConnectionManager cm;  // 连接层:跟踪所有活跃连接

        auto listener = std::make_shared<gomoku::Listener>(
            io, tcp::endpoint(tcp::v4(), static_cast<unsigned short>(port)), cm);
        listener->run();

        spdlog::info("gomoku-server 启动,监听端口 {}", port);
        io.run();
    } catch (const std::exception& e) {
        spdlog::error("启动失败: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
