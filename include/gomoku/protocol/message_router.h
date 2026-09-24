#pragma once

#include <gomoku/protocol/request_context.h>

#include <functional>
#include <string>
#include <unordered_map>

namespace gomoku {

class Session;

// 协议层:消息路由。负责「文本 → Message → RequestContext → 分发到 handler」的完整链路,
// 是 Session(连接层)入站消息的唯一出口。单进程单实例(由 Server 持有)。
class MessageRouter {
public:
    // 处理器签名:处理一条请求,回复通过 ctx.session.send_text(...) 回传。
    // 不设返回值,是为了后续业务 handler 能借助 lambda 捕获额外依赖(DB 池、匹配队列等),
    // 也便于将来实现「匹配成功后主动推送」这类异步回复。
    using Handler = std::function<void(const RequestContext&)>;

    MessageRouter() = default;
    MessageRouter(const MessageRouter&) = delete;
    MessageRouter& operator=(const MessageRouter&) = delete;

    // 注册(或覆盖)某个 type 对应的处理器。
    void register_handler(const std::string& type, Handler handler);

    // 入站入口:解码文本 → 组装 RequestContext → 分发。供 Session 在收到消息后调用。
    void handle_text(const std::string& text, Session& session);

private:
    void dispatch(const RequestContext& ctx) const;

    std::unordered_map<std::string, Handler> _handlers;
};

}  // namespace gomoku
