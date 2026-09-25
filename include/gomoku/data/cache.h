#pragma once

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace gomoku {

namespace asio = boost::asio;

// 数据层:Redis 客户端。与 DBPool 同模式——把阻塞的 hiredis 调用隔离到后台线程,
// 结果通过 asio::post 送回事件循环线程,保证业务逻辑单线程不阻塞、无锁。
//
// 与 DBPool 的差异:Redis 命令快(亚毫秒)且命令之间可能有顺序依赖(读改写),
// 故只用单工作线程 + 单连接串行执行,而非 MySQL 那种「多线程 + thread_local 连接」并行。
// 单进程单实例(由 Server 持有),生命周期贯穿整个进程。
class Cache {
public:
    // Redis 回复的值类型。redisReply* 是 hiredis 内部内存,必须在 worker 线程内
    // freeReplyObject 释放,故先深拷贝成该值类型再跨线程传给回调。
    struct Reply {
        enum class Kind { Nil, Error, Status, String, Integer, Array };
        Kind kind = Kind::Nil;
        std::string str;              // Status / String / Error 的文本
        long long integer = 0;        // Integer
        std::vector<Reply> elements;  // Array 的递归元素
    };
    using ReplyCallback = std::function<void(std::error_code, Reply)>;

    explicit Cache(asio::io_context& io);
    ~Cache() = default;  // thread_pool 析构自动 join,thread_local 连接随线程退出关闭

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    // 异步执行一条命令:argv[0] 为命令名(如 "GET"),后续为参数。
    // 结果在事件循环线程通过回调返回(ec 为空表示成功,reply 为 Redis 返回值)。
    void async_command(std::vector<std::string> argv, ReplyCallback cb);

    // 关闭数据层:停止线程并等待退出(thread_local 连接随之关闭)。
    void stop();

private:
    asio::io_context& _io;
    asio::thread_pool _pool;  // 后台线程池(单线程,见类注释)
};

}  // namespace gomoku
