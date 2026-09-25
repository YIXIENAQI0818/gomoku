#pragma once

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace gomoku {

namespace asio = boost::asio;

// 数据层:MySQL 连接池。把阻塞的 MySQL 查询隔离到后台线程池,结果通过
// asio::post 送回事件循环线程,保证业务逻辑单线程不阻塞、无锁。
// 单进程单实例(由 Server 持有),生命周期贯穿整个进程。
class DBPool {
public:
    using Row = std::vector<std::string>;
    using Result = std::vector<Row>;
    // 回调:ec 为空表示成功;rows 是查询结果(行 × 列字符串,仅 SELECT 非空)。
    using QueryCallback = std::function<void(std::error_code, Result)>;

    explicit DBPool(asio::io_context& io);
    ~DBPool() = default;  // thread_pool 析构自动 join,thread_local 连接随线程退出关闭

    DBPool(const DBPool&) = delete;
    DBPool& operator=(const DBPool&) = delete;

    // 异步查询:提交一条 SQL,结果在事件循环线程通过回调返回。
    void async_query(std::string sql, QueryCallback cb);

    // 关闭数据层:停止线程池并等待线程退出(thread_local 连接随之关闭)。
    void stop();

private:
    asio::io_context& _io;
    asio::thread_pool _pool;  // 后台线程池,执行阻塞 MySQL
};

}  // namespace gomoku
