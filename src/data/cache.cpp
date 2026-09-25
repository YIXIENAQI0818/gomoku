#include <gomoku/data/cache.h>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

#include <spdlog/spdlog.h>

#include <fcntl.h>

#include <chrono>
#include <utility>

namespace gomoku {
namespace {

// Redis 连接信息(配置参数,先硬编码,后续抽 config)。IP 字面量,无 DNS。
constexpr const char* kHost = "127.0.0.1";
constexpr int kPort = 6379;

// 断线后重连间隔。
constexpr auto kReconnectDelay = std::chrono::seconds(1);

// 递归把 redisReply* 深拷贝为值类型 Reply。hiredis 会在回调返回后自动 free 原 reply,
// 所以这里必须拷贝内容,不能持有其指针。
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

}  // namespace

Cache::Cache(asio::io_context& io) : _io(io), _reconnect_timer(io) {
    connect();
}

Cache::~Cache() {
    stop();
}

void Cache::connect() {
    _ac = redisAsyncConnect(kHost, kPort);
    if (!_ac) {
        spdlog::error("Redis 连接失败: 无法分配上下文");
        schedule_reconnect();
        return;
    }
    if (_ac->err) {
        // 同步阶段就失败(如 getaddrinfo 出错),此时 ev.cleanup 尚未设置,hiredis 会跳过。
        spdlog::error("Redis 连接失败: {}", _ac->errstr);
        redisAsyncFree(_ac);
        _ac = nullptr;
        schedule_reconnect();
        return;
    }

    // 确保 socket 非阻塞(stream_descriptor 的 async 操作要求;hiredis 通常已设,保险起见再设一次)。
    int flags = fcntl(_ac->c.fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(_ac->c.fd, F_SETFL, flags | O_NONBLOCK);
    }

    // 挂接事件适配器:hiredis 通过这些回调向外层请求「可读/可写」通知,privdata 传 this。
    _ac->ev.data = this;
    _ac->ev.addRead = &Cache::ev_add_read;
    _ac->ev.delRead = &Cache::ev_del_read;
    _ac->ev.addWrite = &Cache::ev_add_write;
    _ac->ev.delWrite = &Cache::ev_del_write;
    _ac->ev.cleanup = &Cache::ev_cleanup;
    // scheduleTimer 留空(NULL):不用 hiredis 的定时器,重连交给 asio 的 steady_timer。

    // 把 socket fd 接入 io_context(只等可读/可写,不替 hiredis 收数据)。
    _socket = std::make_unique<asio::posix::stream_descriptor>(_io, _ac->c.fd);

    // data 槽位存 this,供 on_connect/on_disconnect 找回对象。
    _ac->data = this;

    // setConnectCallback 会触发第一次 addWrite(等待非阻塞 connect 完成)。
    redisAsyncSetConnectCallback(_ac, &Cache::on_connect);
    redisAsyncSetDisconnectCallback(_ac, &Cache::on_disconnect);
}

void Cache::arm_read() {
    if (_read_armed || !_ac || !_socket) return;
    _read_armed = true;
    _socket->async_wait(asio::posix::stream_descriptor::wait_read,
        [this](const boost::system::error_code& ec) {
            _read_armed = false;
            if (ec == asio::error::operation_aborted || !_ac) return;
            // 即便 ec 是错误也交给 hiredis 检测(内部 read 会失败并触发 on_disconnect)。
            redisAsyncHandleRead(_ac);
        });
}

void Cache::arm_write() {
    if (_write_armed || !_ac || !_socket) return;
    _write_armed = true;
    _socket->async_wait(asio::posix::stream_descriptor::wait_write,
        [this](const boost::system::error_code& ec) {
            _write_armed = false;
            if (ec == asio::error::operation_aborted || !_ac) return;
            redisAsyncHandleWrite(_ac);
        });
}

void Cache::schedule_reconnect() {
    if (_stopping) return;
    _reconnect_timer.expires_after(kReconnectDelay);
    _reconnect_timer.async_wait([this](const boost::system::error_code& ec) {
        if (ec == asio::error::operation_aborted || _stopping) return;
        connect();
    });
}

void Cache::on_connection_lost(struct redisAsyncContext* ac, int status) {
    if (_stopping) return;
    spdlog::warn("Redis 连接断开(status={}),1s 后重连", status);

    // 先解除 asio 对 fd 的所有权,再让 hiredis 关闭 fd 并释放 context,避免双关。
    if (_socket) {
        _socket->release();
        _socket.reset();
    }
    if (ac) {
        redisAsyncFree(ac);  // 内部触发 ev.cleanup(no-op) 并关闭 fd
    }
    _read_armed = _write_armed = false;
    schedule_reconnect();
}

// —— hiredis 回调(事件循环线程执行)——

void Cache::on_connect(const struct redisAsyncContext* ac, int status) {
    auto* self = static_cast<Cache*>(ac->data);
    if (status == REDIS_OK) {
        spdlog::info("Redis 已连接");
    } else {
        // 连接失败:hiredis 随后会调用 on_disconnect,走统一的重连逻辑。
        spdlog::warn("Redis 连接失败");
    }
}

void Cache::on_disconnect(const struct redisAsyncContext* ac, int status) {
    auto* self = static_cast<Cache*>(ac->data);
    auto* old_ac = const_cast<redisAsyncContext*>(ac);
    self->_ac = nullptr;  // 立即标记为未连接,后续命令直接判定失败
    // 延迟到事件循环处理,避免在 hiredis 回调内部直接 free context。
    asio::post(self->_io, [self, old_ac, status] { self->on_connection_lost(old_ac, status); });
}

void Cache::on_reply(struct redisAsyncContext* /*ac*/, void* reply_ptr, void* privdata) {
    // privdata 指向 Pending(每命令上下文),回调里统一释放。
    std::unique_ptr<Pending> pending(static_cast<Pending*>(privdata));
    redisReply* reply = static_cast<redisReply*>(reply_ptr);
    if (!reply) {
        // 连接断开时 hiredis 会以 NULL reply 清空所有挂起回调。
        pending->cb(std::make_error_code(std::errc::io_error), {});
        return;
    }
    Cache::Reply out = convert_reply(reply);  // 深拷贝:hiredis 回调后即 free reply
    pending->cb({}, std::move(out));
}

// —— 事件适配器回调(privdata = this)——

void Cache::ev_add_read(void* p) { static_cast<Cache*>(p)->arm_read(); }
void Cache::ev_del_read(void* p) { (void)p; }  // 单发 async_wait 无需主动注销
void Cache::ev_add_write(void* p) { static_cast<Cache*>(p)->arm_write(); }
void Cache::ev_del_write(void* p) { (void)p; }  // 单发 async_wait 无需主动注销
void Cache::ev_cleanup(void* p) { (void)p; }    // fd 由 on_connection_lost / stop 统一处理

void Cache::async_command(std::vector<std::string> argv, ReplyCallback cb) {
    if (!_ac) {
        cb(std::make_error_code(std::errc::not_connected), {});
        return;
    }

    auto* pending = new Pending{std::move(cb)};

    std::vector<const char*> cargv;
    cargv.reserve(argv.size());
    for (const auto& a : argv) {
        cargv.push_back(a.c_str());
    }

    // 命令立即被格式化进发送缓冲区,argv/cargv 在调用后即可销毁。
    int status = redisAsyncCommandArgv(_ac, &Cache::on_reply, pending,
        static_cast<int>(cargv.size()), cargv.data(), nullptr);
    if (status != REDIS_OK) {
        pending->cb(std::make_error_code(std::errc::io_error), {});
        delete pending;
    }
}

void Cache::stop() {
    if (_stopping) return;
    _stopping = true;
    _reconnect_timer.cancel();
    if (_socket) {
        _socket->release();
        _socket.reset();
    }
    if (_ac) {
        redisAsyncFree(_ac);
        _ac = nullptr;
    }
}

}  // namespace gomoku
