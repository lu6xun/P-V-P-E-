# TCP JSON 协议 v1

每帧是 **4 字节无符号网络字节序（big-endian）正文长度 + UTF-8 JSON 对象**。长度不包括前缀，范围 1～65536 字节；先验证长度，再分配正文。头和正文分别 `async_read` 到完整长度，因此支持任意拆包、粘包。非法 JSON、非对象根、深度大于 32、长度非法直接关闭该连接，不影响其他连接。

客户端只上传意图，所有 C→S 消息严格拒绝额外字段，不允许上传 `playerId`、最终伤害、生命或能量。连接身份由服务端绑定，座位由加入结果返回。所有时间单位都是毫秒；无跨设备绝对时钟假设。

## 消息表

| 方向 | type | 字段 / 说明 |
| --- | --- | --- |
| S→C | `welcome` | `protocol:1, heartbeatMs, heartbeatTimeoutMs` |
| C→S | `create_room` | 仅 type；一个连接只能属于一个房间 |
| S→C | `room_created` | `room:"004291", seat:0` |
| C→S | `join_room` | `room` 必须是六位 ASCII 数字字符串，保留前导零 |
| S→C | `room_joined` | `room, seat:1` |
| S→C | `room_state` | `room, players:1或2, phase:"waiting"或"starting"` |
| S→C | `match_start` | `room, round, roundId, state, submitted:[false,false], remainingMs, rules, timing` |
| C→S | `submit` | `roundId, action`；`action` 为 `attack/defend/charge/ultimate` |
| S→C | `submit_ack` | `roundId, automatic:boolean`；不包含隐藏行动 |
| S→C | `opponent_submitted` | 只有 `roundId`，不包含隐藏行动 |
| S→C | `round_result` | 完整双方结果，见下例；向双方发送同一对象 |
| S→C | `round_start` | `room, round, roundId, state, submitted:[false,false], remainingMs` |
| S→C | `match_finished` | `room, outcome`；服务端展示期结束，可以发再战请求 |
| C→S | `rematch` | `agree:boolean`；只在 Finished 接受 |
| S→C | `rematch_state` | `accepted:[bool,bool]`；双方同意后发送新 match_start |
| C→S | `leave_room` | 仅 type；销毁房间并通知双方 |
| S→C | `room_left` | `reason`；发给主动离开者 |
| S→C | `opponent_left` | `reason`；断线、拒绝、退出或等待房间过期；中止而非判胜 |
| S→C | `error` | `code, message`；语义请求被拒绝，连接仍可用 |
| C→S | `ping` | 仅 type |
| S→C | `pong` | 仅 type |

`roundId` 必须是 JSON 整数，1～9007199254740991。当前服务端仅发递增整数，不复用已完成轮次的标识。`round` 是供界面显示的每场轮号。

`state`、`before`、`actions`、`damage`、`energyDelta`、`blocked` 的数组顺序**固定为创建者（0）、加入者（1）**，不是收信者先。客户端根据自己的 `seat` 显示左侧自己、右侧对方。`damage[i]` 是玩家 i **受到**的规则伤害。

## 一轮结果示例

```json
{
  "type": "round_result", "room": "004291", "round": 4, "roundId": 19,
  "before": [{"hp": 5, "energy": 3}, {"hp": 5, "energy": 0}],
  "actions": ["ultimate", "defend"],
  "state": [{"hp": 5, "energy": 0}, {"hp": 2, "energy": 0}],
  "damage": [0, 3], "energyDelta": [-3, 0], "blocked": [false, false],
  "outcome": "ongoing", "revealMs": 700, "displayMs": 1700
}
```

胜负枚举为 `ongoing/player0/player1/draw`。`blocked[i]` 只表示本轮 Defend 确实挡住了普通攻击；双方防御时都为 false，大招不会被格挡。

## 拒绝与保密

- `invalid_message`：类型、字段、枚举、房间格式或轮次范围错误，包括未知额外字段。
- `already_in_room` / `not_in_room`：连接归属错误；重复加入不新增玩家。
- `room_not_found` / `room_started` / `room_full` / `server_full`：找不到房间、已开始（也表示两席被占用）、已满或达到房间容量。本实现第二人加入立即开始，因此正常房间满时返回 `room_started`。
- `wrong_round`：过期或未来令牌；`wrong_phase`：展示期/结束期禁止提交。
- `already_submitted`：当前轮已锁定；`illegal_action`：能量不够或满能量仍蓄力。
- `already_requested`：已请求再战。

第一个提交触发的对方消息严格只有 `type` 和 `roundId`。直到双方行动确定（包括超时默认防御），才生成并广播 `round_result`。没有把明文行动藏在“不可见 UI”字段中的做法。

每次接收合法对象更新连接活动时间；客户端周期性 ping，服务端 pong。双方检测失联；无重连。写队列最多 128 帧/256 KiB，客户端事件队列最多 256 条；溢出会中止异常连接。服务端连接数最多 256，房间数最多 128，每连接每秒至多 120 条请求。

这是明文 TCP 的教学协议，不提供 TLS、账号认证、抗恶意服务器或生产级流量治理。
