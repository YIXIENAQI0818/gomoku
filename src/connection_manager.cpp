#include <gomoku/connection_manager.h>
#include <gomoku/session.h>

namespace gomoku {

void ConnectionManager::join(const std::shared_ptr<Session>& session) {
    _sessions.insert(session);
}

void ConnectionManager::leave(const std::shared_ptr<Session>& session) {
    _sessions.erase(session);
}

void ConnectionManager::stop_all() {
    // 先拷贝一份:close() 会触发 Session 的回调 → leave() → 修改 _sessions,
    // 若直接遍历原集合会导致迭代器失效。
    auto sessions = _sessions;
    for (const auto& session : sessions) {
        session->close();
    }
}

}  // namespace gomoku
