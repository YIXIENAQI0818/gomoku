#include <gomoku/data/cache.h>

#include <hiredis/hiredis.h>

#include <spdlog/spdlog.h>

#include <sys/time.h>

#include <utility>

namespace gomoku {
namespace {

// Redis 连接信息(配置参数,先硬编码,后续抽 config)。
constexpr const char* kHost = "127.0.0.1";
constexpr int kPort = 6379;

// 工作线程数 = 1。Redis 命令快,串行足够;更重要的是单连接保证命令按提交顺序执行
// (后续匹配队列 SPOP、会话 INCR 等读改写依赖顺序),MySQL 的并行模型在这里反而会乱序。
constexpr std::size_t kPoolSize = 1;

// 连接超时:1.5 秒。避免 Redis 不可用时 worker 线程永久卡死在阻塞 connect 上。
constexpr timeval kConnectTimeout{1, 500000};

// thread_local 连接:单工作线程 → 单 redisContext,惰性建立、复用、RAII 关闭。
class Connection {
public:
    Connection() = default;
    ~Connection() { close(); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    redisContext* get() {
        if (_ctx) return _ctx;
        _ctx = redisConnectWithTimeout(kHost, kPort, kConnectTimeout);
        if (!_ctx || _ctx->err) {
            if (_ctx) {
                spdlog::error("Redis 连接失败: {}", _ctx->errstr);
            } else {
                spdlog::error("Redis 连接失败: 无法分配上下文");
            }
            close();
            return nullptr;
        }
        return _ctx;
    }

    // 命令失败(连接丢失)时调用:关闭连接,下次 get() 重新建立。
    void reset() { close(); }

private:
    void close() {
        if (_ctx) {
            redisFree(_ctx);
            _ctx = nullptr;
        }
    }
    redisContext* _ctx = nullptr;
};

thread_local Connection t_conn;

// 递归把 redisReply* 深拷贝为值类型 Reply(worker 线程内调用)。
Cache::Reply convert_reply(const redisReply* r) {
    Cache::Reply out;
    if (!r) return out;
    switch (r->type) {
    case REDIS_REPLY_NIL:
        out.kind = Cache::Reply::Kind::Nil;
        break;
    case REDIS_REPLY_ERROR:
        out.kind = Cache::Reply::Kind::Error;
        out.str.assign(r->str, r->len);
        break;
    case REDIS_REPLY_STATUS:
        out.kind = Cache::Reply::Kind::Status;
        out.str.assign(r->str, r->len);
        break;
    case REDIS_REPLY_STRING:
        out.kind = Cache::Reply::Kind::String;
        out.str.assign(r->str, r->len);
        break;
    case REDIS_REPLY_INTEGER:
        out.kind = Cache::Reply::Kind::Integer;
        out.integer = r->integer;
        break;
    case REDIS_REPLY_ARRAY:
        out.kind = Cache::Reply::Kind::Array;
        out.elements.reserve(r->elements);
        for (std::size_t i = 0; i < r->elements; ++i) {
            out.elements.push_back(convert_reply(r->element[i]));
        }
        break;
    }
    return out;
}

// 在后台线程执行一条命令,返回 (错误码, 回复值)。ec 仅在传输/连接失败时非空;
// Redis 服务端返回的 -ERR 属于业务语义,原样透传给业务层按 reply.kind 判断。
std::pair<std::error_code, Cache::Reply> execute(const std::vector<std::string>& argv) {
    redisContext* ctx = t_conn.get();
    if (!ctx) {
        return {std::make_error_code(std::errc::io_error), {}};
    }

    std::vector<const char*> cargv;
    cargv.reserve(argv.size());
    for (const auto& a : argv) {
        cargv.push_back(a.c_str());
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommandArgv(ctx, static_cast<int>(cargv.size()), cargv.data(), nullptr));
    if (!reply) {
        // redisCommandArgv 返回 null = 连接类错误:关闭连接,下次查询自动重连。
        spdlog::error("Redis 命令失败: {}", ctx->errstr);
        t_conn.reset();
        return {std::make_error_code(std::errc::io_error), {}};
    }

    Cache::Reply out = convert_reply(reply);
    freeReplyObject(reply);  // 释放 hiredis 内部内存(值已拷贝,可安全释放)
    return {{}, std::move(out)};
}

}  // namespace

Cache::Cache(asio::io_context& io) : _io(io), _pool(kPoolSize) {}

void Cache::stop() {
    _pool.stop();   // 停止接受新任务
    _pool.join();   // 等待线程退出(thread_local 连接随之关闭)
}

void Cache::async_command(std::vector<std::string> argv, ReplyCallback cb) {
    // 第一次 post:把阻塞命令扔进线程池,事件循环立即返回、继续服务其他连接。
    asio::post(_pool, [this, argv = std::move(argv), cb = std::move(cb)] {
        auto [ec, reply] = execute(argv);
        // 第二次 post:结果送回事件循环线程,回调在此执行 → 业务逻辑仍单线程无锁。
        asio::post(_io, [cb = std::move(cb), ec, reply = std::move(reply)] {
            cb(ec, std::move(reply));
        });
    });
}

}  // namespace gomoku
