#include <gomoku/protocol/message.h>
#include <gomoku/protocol/request_context.h>
#include <gomoku/server.h>
#include <gomoku/data/db_pool.h>
#include <gomoku/net/session.h>

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
        // 注:心跳检测由 Session 的 beast 协议层 timeout 负责,不在此注册应用层 ping。
        server.router().register_handler("echo",
            [](const gomoku::RequestContext& ctx) {
                ctx.session.send_text(gomoku::serialize_message("echo", ctx.data));
            });
        // 2.1 临时验证:触发一次异步 MySQL 查询,验证「线程池隔离阻塞」链路。2.2 移除。
        server.router().register_handler("db.test",
            [&server](const gomoku::RequestContext& ctx) {
                auto session = ctx.session.shared_from_this();
                server.db().async_query("SELECT VERSION()",
                    [session](std::error_code ec, gomoku::DBPool::Result rows) {
                        nlohmann::json data;
                        if (ec) {
                            data = {{"error", ec.message()}};
                        } else {
                            data = {{"version",
                                rows.empty() || rows[0].empty() ? "" : rows[0][0]}};
                        }
                        session->send_text(gomoku::serialize_message("db.test", data));
                    });
            });
        // 2.2 临时验证:SET→GET 值往返,验证「hiredis→asio 异步桥接 + 链式回调」链路。2.3 移除。
        server.router().register_handler("redis.test",
            [&server](const gomoku::RequestContext& ctx) {
                auto session = ctx.session.shared_from_this();
                auto fail = [session](const std::string& err) {
                    session->send_text(gomoku::serialize_message(
                        "redis.test", nlohmann::json{{"error", err}}));
                };
                server.cache().async_command({"SET", "gomoku:test", "hello"},
                    [&server, session, fail](std::error_code ec, gomoku::Cache::Reply) {
                        if (ec) { fail(ec.message()); return; }
                        server.cache().async_command({"GET", "gomoku:test"},
                            [session, fail](std::error_code ec2, gomoku::Cache::Reply reply) {
                                if (ec2) { fail(ec2.message()); return; }
                                nlohmann::json data;
                                if (reply.kind == gomoku::Cache::Reply::Kind::Nil) {
                                    data = {{"error", "key 不存在(意外)"}};
                                } else {
                                    data = {{"value", reply.str}};
                                }
                                session->send_text(gomoku::serialize_message("redis.test", data));
                            });
                    });
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
