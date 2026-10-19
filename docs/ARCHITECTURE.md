# 状态机与线程

## 客户端

```mermaid
stateDiagram-v2
    [*] --> Menu
    Menu --> LocalSelecting: Play vs AI / 预先生成 AI 行动
    Menu --> Connecting: Create / Join
    Connecting --> RoomWaiting: 房间响应
    Connecting --> ConnectionError: 连接失败 / 房间错误
    RoomWaiting --> OnlineSelecting: match_start
    LocalSelecting --> Reveal: 确认 / 单机超时防御
    OnlineSelecting --> Submitted: 发送行动意图
    Submitted --> Reveal: round_result
    OnlineSelecting --> Reveal: 超时后 round_result
    Reveal --> Settlement: 非阻塞计时
    Settlement --> LocalSelecting: 未结束 / 新一轮 AI 提前选择
    Settlement --> OnlineSelecting: 服务端 round_start
    Settlement --> Finished: 生命归零
    Finished --> LocalSelecting: 单机重开
    Finished --> OnlineSelecting: 双方再战 / match_start
    Submitted --> ConnectionError: 对手离开 / 心跳超时
    OnlineSelecting --> ConnectionError: 对手离开 / 网络错误
    ConnectionError --> Menu: 重试
    RoomWaiting --> Menu: 主动离开
    Finished --> Menu: 拒绝再战 / 返回
```

所有游戏页都支持 Esc 返回菜单。网络权威事件可以中断本地动画：下一轮开始前，未展示的上一轮结算会记入日志，然后切换当前轮次。`Finished` 仍等待服务端 `match_finished` 才允许请求再战，避免本地展示时钟提前结束。

## 服务端房间

```mermaid
stateDiagram-v2
    [*] --> Waiting: 创建六位字符串编号
    Waiting --> Selecting: 第二人加入 / 初始化生命与能量
    Selecting --> Selecting: 一方提交 / 只通知已提交
    Selecting --> Showing: 双方提交或超时 / 原子规则结算
    Showing --> Selecting: 展示截止 / 生成新 roundId
    Showing --> Finished: 展示截止且终局
    Finished --> Finished: 一方同意再战
    Finished --> Selecting: 双方同意 / 完整重置
    Waiting --> [*]: 等待超时 / 离开 / 断线
    Selecting --> [*]: 离开 / 断线
    Showing --> [*]: 离开 / 断线
    Finished --> [*]: 拒绝 / 离开 / 断线
```

每轮从 `Selecting` 到 `Showing` 只转换一次；`actions` 是每席一个 optional，写入后不能覆盖。服务端单线程串行处理计时器和 socket 回调；收到合法请求时额外检查当前截止时间，避免晚到请求钻定时器间隔的空隙。超时填充防御沿用同一 `submit` 路径。

`round` 是每场从 1 开始的展示编号；`roundId` 是服务端进程内递增的请求令牌，再战时也不复用，防止旧行动包落入新对局。重开清空行动、同意状态、状态、回合序号和截止时间，再分配新令牌。服务端重启不保留连接或房间。

## 数据与线程边界

```mermaid
flowchart LR
    UI[主线程: raylib / Game] -->|asio::post 行动意图| NET[客户端网络线程]
    NET -->|互斥保护 JSON 事件队列| UI
    NET <-->|长度前缀 TCP| IO[服务端单线程 io_context]
    IO --> ROOMS[RoomService / 单调时钟]
    ROOMS --> PURE[resolveRound 纯规则]
    UI --> LOCAL[LocalMatch / AI 开始时选招]
    LOCAL --> PURE
    TEST[CTest] --> PURE
    TEST --> ROOMS
```

所有 raylib API 都位于主线程。网络线程只产生 JSON 事件；主线程每帧 drain 队列。析构时先停止 io_context，再 join 工作线程，最后由 RAII 释放 socket、定时器与处理器。连接读始终只有一个 `async_read` 链；写入始终只有一个 `async_write`，FIFO 保持帧顺序与缓冲区生命周期。

服务端向客户端发消息通过 `asio::post`，避免发送队列溢出同步触发断线时重入房间方法、销毁当前正在访问的房间引用。断线回收先移除房间，再通知剩余连接。每 20ms 处理心跳和房间截止事件，未过期的状态不变化。
