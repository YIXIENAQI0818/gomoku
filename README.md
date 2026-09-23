# 五子棋在线对战平台 (Gomoku Online)

一个**可上线、前后端齐全**的五子棋在线对战平台。C++ 后端(WebSocket 长连接)+ 浏览器前端 + Redis/MySQL 真实中间件。

## 功能

- **账号**:注册 / 登录 / 战绩
- **匹配**:自动匹配真人对手
- **对弈**:15×15 棋盘实时对战,对方落子实时同步
- **聊天**:对局内聊天
- **排行榜**:胜场 / 胜率实时排名

## 技术栈

- **C++17** + boost.asio / boost.beast(WebSocket 长连接)
- **nlohmann/json** 序列化
- **Redis**(匹配队列 / 在线状态 / 排行榜)+ **MySQL**(账号 / 战绩)
- **原生 HTML/CSS/JS** 前端
- **CMake** 构建,**Docker** 部署

## 架构

```
浏览器(HTML/JS, WebSocket 客户端)
        │  WebSocket + JSON 消息
        ▼
┌────────────── C++ 后端(单进程, Reactor 事件循环)──────────────┐
│  连接层  ConnectionManager    连接 / 心跳 / 断线管理            │
│  协议层  MessageRouter        JSON 编解码 + 消息路由分发        │
│  业务层  Account / Match / Game / Chat / Rank 服务             │
│  数据层  DBPool / Cache       MySQL 连接池 + Redis 封装         │
└───────────────┬──────────────────────────┬──────────────────┘
                ▼                          ▼
            Redis(hiredis)              MySQL
          匹配队列/在线状态/排行榜      账号/战绩持久化
```

## 快速开始

### 依赖(WSL2 Ubuntu)

```bash
sudo apt install -y libboost-all-dev nlohmann-json3-dev libhiredis-dev libspdlog-dev
sudo apt install -y redis-server mysql-server libmysqlclient-dev
```

### 构建与运行

```bash
cmake -S . -B build && cmake --build build -j
./build/gomoku-server 8080        # 启动服务,默认端口 8080
```

浏览器访问 `http://localhost:8080` 即可游玩。

## 协议

WebSocket 传输 JSON,消息格式 `{ "type": "...", "data": {...} }`,按 `type` 路由:

| type | 方向 | 说明 |
|------|------|------|
| `auth.register` / `auth.login` | C→S | 注册 / 登录 |
| `match.join` / `match.cancel` | C→S | 进入 / 取消匹配 |
| `game.move` | C→S | 落子 |
| `game.start` / `game.move` / `game.over` | S→C | 对局开始 / 对方落子 / 对局结束 |
| `chat.send` / `chat.message` | C→S / S→C | 聊天 |
| `rank.list` | C→S | 排行榜查询 |

## 目录结构

```
gomoku/
├── CMakeLists.txt          # 构建脚本
├── include/gomoku/         # 头文件(核心库接口)
├── src/                    # 源码
├── web/                    # 后端入口 + 前端静态文件
│   └── static/             # index.html / style.css / app.js
├── docker/                 # Dockerfile + docker-compose
├── data/                   # SQL 建表脚本等
└── tests/                  # 单元测试
```

## License

MIT
