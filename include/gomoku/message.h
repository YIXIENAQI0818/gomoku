#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace gomoku {

// 协议层消息:统一格式 { "type": "...", "data": {...} }
struct Message {
    std::string type;
    nlohmann::json data;
};

// 解码:原始文本 → Message。非法 JSON 或缺少 type 时返回 nullopt。
std::optional<Message> parse_message(const std::string& text);

// 编码:type + data → JSON 字符串。
std::string serialize_message(const std::string& type, const nlohmann::json& data);

}  // namespace gomoku
