#pragma once

#include <boost/asio.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>

#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

struct redisAsyncContext;  // hiredis 异步上下文(前向声明,头文件不引入 hiredis,与 db_pool.h 一致)

namespace gomoku {

namespace asio = boost::asio;

// 数据层:Redis 客户端。用 hiredis 的「异步 API」(redisAsyncContext),把 socket 直接
// 接入 asio 的 io_context,Redis 命令完全非阻塞地跑在事件循环上——**不开 worker 线程**。
// 这与 MySQL(DBPool)不同:libmysqlclient 只能阻塞,故用线程池隔离;Redis 足够成熟、
// 有异步客户端,就顺着事件循环走,不额外开线程。
//
// hiredis async 不绑定具体事件库,通过「事件适配器」回调(addRead/addWrite 等)向外层
// 请求「可读/可写」通知;本类用 asio::posix::stream_descriptor 的 async_wait 实现这些
// 回调,可读/可写时再调 redisAsyncHandleRead/Write 交给 hiredis 自己收发。
// 单进程单实例(由 Server 持有),生命周期贯穿整个进程。
class Cache {
public:
    // Redis 回复的值类型。redisReply* 是 hiredis 内部内存,会在回调返回后被 hiredis
    // 自动释放,故必须在回调内深拷贝成该值类型再跨出去。
    struct Reply {
        enum class Kind { Nil, Error, Status, String, Integer, Array };
        Kind kind = Kind::Nil;
        std::string str;              // Status / String / Error 的文本
        long long integer = 0;        // Integer
        std::vector<Reply> elements;  // Array 的递归元素
    };
    using ReplyCallback = std::function<void(std::error_code, Reply)>;

    explicit Cache(asio::io_context& io);
    ~Cache();

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    // 异步执行一条命令:argv[0] 为命令名(如 "GET"),后续为参数。
    // 结果在事件循环线程通过回调返回(ec 为空表示成功,reply 为 Redis 返回值)。
    void async_command(std::vector<std::string> argv, ReplyCallback cb);

    // 关闭数据层:断开连接(不再重连)。
    void stop();

private:
    // 每个待回复命令的上下文(挂在 redisAsyncCommandArgv 的 privdata 上,回调里释放)。
    struct Pending {
        ReplyCallback cb;
    };

    // —— hiredis 回调(static,通过 ac->data / privdata 找回 this)——
    static void on_connect(const struct redisAsyncContext* ac, int status);
    static void on_disconnect(const struct redisAsyncContext* ac, int status);
    static void on_reply(struct redisAsyncContext* ac, void* reply, void* privdata);

    // —— 事件适配器回调(privdata = this)——
    static void ev_add_read(void* p);
    static void ev_del_read(void* p);
    static void ev_add_write(void* p);
    static void ev_del_write(void* p);
    static void ev_cleanup(void* p);

    void connect();                          // 建立异步连接 + 挂接适配器
    void arm_read();                         // 注册一次可读等待(async_wait wait_read)
    void arm_write();                        // 注册一次可写等待(async_wait wait_write)
    void on_connection_lost(struct redisAsyncContext* ac, int status);
    void schedule_reconnect();

    asio::io_context& _io;
    struct redisAsyncContext* _ac = nullptr;      // 当前连接(未连接时为 nullptr)
    std::unique_ptr<asio::posix::stream_descriptor> _socket;  // 包住 _ac->c.fd,只等可读/可写
    asio::steady_timer _reconnect_timer;          // 断线重连定时器
    bool _read_armed = false;                     // 是否有挂起的可读 async_wait
    bool _write_armed = false;                    // 是否有挂起的可写 async_wait
    bool _stopping = false;
};

}  // namespace gomoku
