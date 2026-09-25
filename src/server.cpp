#include <gomoku/server.h>
#include <gomoku/net/listener.h>
#include <gomoku/net/session.h>

#include <utility>

namespace gomoku {

Server::Server(asio::io_context& io, tcp::endpoint endpoint)
    : _db(io),
      _cache(io),
      _listener(std::make_shared<Listener>(io, endpoint,
          [this](tcp::socket socket) {
              // 创建 Session 的职责归属 Server:Listener 只负责把 socket 交出来。
              std::make_shared<Session>(std::move(socket), _cm, _router)->run();
          })) {}

void Server::run() {
    _listener->run();
}

void Server::stop() {
    _listener->stop();  // 1. 停止接受新连接
    _cm.stop_all();     // 2. 关闭已有连接
    _db.stop();         // 3. 关闭数据层:MySQL(停止线程池)
    _cache.stop();      // 4. 关闭数据层:Redis(停止线程池)
}

}  // namespace gomoku
