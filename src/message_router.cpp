#include <gomoku/message_router.h>
#include <gomoku/message.h>
#include <gomoku/session.h>

#include <spdlog/spdlog.h>

#include <utility>

namespace gomoku {

void MessageRouter::register_handler(const std::string& type, Handler handler) {
    _handlers[type] = std::move(handler);
}

void MessageRouter::handle_text(const std::string& text, Session& session) {
    auto msg = parse_message(text);
    if (!msg) {
        spdlog::warn("收到非法消息,忽略: {}", text);
        return;
    }
    spdlog::info("收到消息 type={}", msg->type);

    // Message → RequestContext:附上发起请求的 Session,构成 handler 的统一入口。
    RequestContext ctx{msg->type, std::move(msg->data), session};
    dispatch(ctx);
}

void MessageRouter::dispatch(const RequestContext& ctx) const {
    auto it = _handlers.find(ctx.type);
    if (it == _handlers.end()) {
        // 合法 JSON 但无对应处理器:与非法消息同等对待,静默忽略。
        // 理由一致——客户端消息不可信,不回 error 也不断开,只留日志。
        spdlog::warn("未知消息类型,忽略: {}", ctx.type);
        return;
    }
    it->second(ctx);
}

}  // namespace gomoku
