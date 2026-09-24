#include <gomoku/protocol/message.h>

namespace gomoku {

std::optional<Message> parse_message(const std::string& text) {
    try {
        auto j = nlohmann::json::parse(text);
        // 必须是对象,且 type 存在且为字符串
        if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) {
            return std::nullopt;
        }
        Message msg;
        msg.type = j["type"].get<std::string>();
        msg.data = j.value("data", nlohmann::json::object());
        return msg;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::string serialize_message(const std::string& type, const nlohmann::json& data) {
    nlohmann::json j;
    j["type"] = type;
    j["data"] = data;
    return j.dump();
}

}  // namespace gomoku
