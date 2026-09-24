#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace gomoku {

class Session;

// 协议层:一个入站请求的上下文,是所有业务 handler 的统一入口参数。
// 由 MessageRouter 在分发前构造:从 Message 提取 type/data,并附上发起请求的 Session。
//
// 除消息本身外,这里还承载「处理这条请求所需的会话 / 身份 / 追踪信息」;
// 后续阶段按需扩展(见下方预留),避免每加一类信息就改一次 handler 签名。
//
// 注意:RequestContext 是同步分发期间的栈上临时对象,handler 应在其生命周期内
// 完成处理;若将来出现异步回复(如匹配成功推送),应拷贝所需字段而非持有本对象引用。
struct RequestContext {
    std::string type;    // 消息类型
    nlohmann::json data; // 消息数据(JSON)
    Session& session;    // 发起请求的连接(裸引用,不持有所有权)

    // —— 预留扩展(阶段 2 账号 / 阶段 3 对弈时逐步填充)——
    // std::optional<UserId> user_id;   // 登录后的用户标识
    // std::string trace_id;            // 请求追踪 id
    // AuthInfo auth;                   // 鉴权结果
    // RequestMetadata metadata;        // 通用元数据(客户端版本等)
};

}  // namespace gomoku
