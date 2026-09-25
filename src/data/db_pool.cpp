#include <gomoku/data/db_pool.h>

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include <spdlog/spdlog.h>

#include <utility>

namespace gomoku {
namespace {

// MySQL 连接信息(配置参数,先硬编码,后续抽 config)。
constexpr const char* kHost = "127.0.0.1";
constexpr const char* kUser = "gomoku";
constexpr const char* kPassword = "gomoku";
constexpr const char* kDatabase = "gomoku";
constexpr unsigned int kPort = 3306;

// 后台线程数(备选 2 / 8,视压测调整)。
constexpr std::size_t kPoolSize = 4;

// thread_local 连接:线程池里每个线程惰性建立自己的 MySQL 连接并复用,全程无锁。
// RAII 包装,线程退出时析构关闭连接。
class Connection {
public:
    Connection() = default;
    ~Connection() { close(); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    MYSQL* get() {
        if (_conn) return _conn;
        _conn = mysql_init(nullptr);
        if (!_conn) return nullptr;
        if (!mysql_real_connect(_conn, kHost, kUser, kPassword, kDatabase, kPort, nullptr, 0)) {
            spdlog::error("MySQL 连接失败: {}", mysql_error(_conn));
            close();
            return nullptr;
        }
        return _conn;
    }

    // 查询失败(连接丢失)时调用:关闭连接,下次 get() 重新建立。
    void reset() { close(); }

private:
    void close() {
        if (_conn) {
            mysql_close(_conn);
            _conn = nullptr;
        }
    }
    MYSQL* _conn = nullptr;
};

thread_local Connection t_conn;

// 在后台线程执行一条阻塞查询,返回 (错误码, 结果)。
std::pair<std::error_code, DBPool::Result> execute(const std::string& sql) {
    MYSQL* conn = t_conn.get();
    if (!conn) {
        return {std::make_error_code(std::errc::io_error), {}};
    }
    if (mysql_query(conn, sql.c_str()) != 0) {
        unsigned int err = mysql_errno(conn);
        spdlog::error("MySQL 查询失败 [{}]: {}", err, mysql_error(conn));
        // 连接丢失类错误:关闭连接,下次查询自动重连。
        if (err == CR_SERVER_GONE_ERROR || err == CR_SERVER_LOST) {
            t_conn.reset();
        }
        return {std::make_error_code(std::errc::io_error), {}};
    }

    DBPool::Result rows;
    if (MYSQL_RES* res = mysql_store_result(conn)) {
        unsigned int ncols = mysql_num_fields(res);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            DBPool::Row r;
            r.reserve(ncols);
            for (unsigned int i = 0; i < ncols; ++i) {
                r.emplace_back(row[i] ? row[i] : "");
            }
            rows.push_back(std::move(r));
        }
        mysql_free_result(res);
    }
    return {{}, std::move(rows)};
}

}  // namespace

DBPool::DBPool(asio::io_context& io) : _io(io), _pool(kPoolSize) {}

void DBPool::stop() {
    _pool.stop();   // 停止接受新任务
    _pool.join();   // 等待线程退出(短暂阻塞,thread_local 连接随之关闭)
}

void DBPool::async_query(std::string sql, QueryCallback cb) {
    // 第一次 post:把阻塞查询扔进线程池,事件循环立即返回、继续服务其他连接。
    asio::post(_pool, [this, sql = std::move(sql), cb = std::move(cb)] {
        auto [ec, rows] = execute(sql);
        // 第二次 post:结果送回事件循环线程,回调在此执行 → 业务逻辑仍单线程无锁。
        asio::post(_io, [cb = std::move(cb), ec, rows = std::move(rows)] {
            cb(ec, std::move(rows));
        });
    });
}

}  // namespace gomoku
