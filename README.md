# Simultaneous Duel / 同时选招对战

一个可本地运行的 C++17 游戏求职作品：raylib 几何图形客户端、独立 Asio TCP 权威服务端、公共纯规则模块。支持玩家对 AI，以及同一服务器上的六位房间号双人对战。没有账号、数据库、聊天或排行榜。

## 快速启动

本机已构建版本位于 `build/Release/`。单机只需运行 `duel_client.exe`，点击 **Play vs AI**。

联机时先启动服务端，再启动两个客户端（在项目根目录打开 PowerShell）：

```powershell
# 终端一，保持运行；Ctrl+C 停止
.\build\Release\duel_server.exe

# 终端二
.\build\Release\duel_client.exe

# 终端三
.\build\Release\duel_client.exe
```

两个客户端保持 `localhost`、端口 `28765`。第一个点击 **Create Room**，复制六位房间号；第二个填入 **6-DIGIT ROOM CODE**，点击 **Join Room**。加入后自动开局，双方每轮选招并确认，五轮互相普通攻击可得到平局。

**房间号只用于查找同一服务器上的房间，不能替代服务器地址，也不能自动穿透 NAT。** 局域网玩家填写服务端电脑的局域网地址；异地玩家需要双方均可访问的服务器地址和开放的 TCP 端口。项目没有自行部署云服务。

## 从源码构建

要求 CMake 3.20+、C++17 编译器、Python 3（仅集成测试使用）；桌面客户端需要 OpenGL 3.3。Windows 优先使用 Visual Studio 2022 / Build Tools 的“使用 C++ 的桌面开发”组件和 Windows SDK。

第三方版本固定，并校验源码归档 SHA-256：

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| [raylib](https://github.com/raysan5/raylib/releases/tag/5.5) | 5.5 | 窗口、输入、几何图形 |
| [独立 Asio](https://github.com/chriskohlhoff/asio/tree/asio-1-30-2) | 1.30.2 | TCP 与异步定时器，不依赖 Boost |
| [nlohmann/json](https://github.com/nlohmann/json/releases/tag/v3.11.3) | 3.11.3 | 配置和网络 JSON |

首次配置由 CMake FetchContent 下载依赖，需要能访问 GitHub。依赖许可证在上游源码中；参见 [第三方说明](docs/THIRD_PARTY.md)。

### Windows

在项目目录运行：

```powershell
.\scripts\build-windows.ps1
```

脚本能查找 Visual Studio 自带且未加入 PATH 的 CMake，并构建、执行 CTest。等价标准命令：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

离线时先解压**对应固定版本**的源码，再显式指定源目录（Asio 目录下应有 `asio/include/asio.hpp`）：

```powershell
.\scripts\build-windows.ps1 -RaylibSource 'D:\deps\raylib-5.5' -AsioSource 'D:\deps\asio-asio-1-30-2' -JsonSource 'D:\deps\json-3.11.3'
```

源目录覆盖不会重新校验下载归档，调用者需保证源码版本。MinGW 必须支持完整 C++17；本次实际验证使用 MSVC，没有验证 MinGW。

### Linux / 仅服务端

```bash
cmake -S . -B build-server -DDUEL_BUILD_CLIENT=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-server -j4
ctest --test-dir build-server --output-on-failure
./build-server/duel_server
```

只构建服务端不需要显示环境或 raylib。若需要 Linux 桌面客户端，安装 raylib/GLFW 所需的 OpenGL、X11 开发包后移除 `-DDUEL_BUILD_CLIENT=OFF`。示例 Ubuntu 包：`build-essential cmake python3 libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev`。Linux/macOS 尚未在本次环境实际构建，可能需要对应平台开发依赖。

`-DBUILD_TESTING=OFF` 可以在不运行测试的部署构建中取消 Python 依赖。

## 操作与战斗规则

| 输入 / 行动 | 效果 |
| --- | --- |
| 鼠标 / 数字 1～4 | 选择行动，确认之前可修改 |
| Enter / CONFIRM | 提交；联机等服务端确认后锁定 |
| Esc / Menu | 返回菜单；主菜单 Esc 退出 |
| Tab / Backspace | 切换输入框 / 删除字符 |
| Ctrl+V | 用剪贴板内容替换当前输入框，保留允许的字符并限制长度 |
| Attack | 免费，对未防御者造成 1 点伤害 |
| Defend | 仅挡普通攻击，无反击、不加能量 |
| Charge | 能量 +1，上限 3；满能量禁用；受伤仍获得能量 |
| Ultimate | 回合开始时必须有 3 能量，消耗 3、伤害 3、无视防御 |

双方默认 5 生命、0 能量。普通攻击、防御不消耗已有能量。所有伤害和能量变化从同一份初始状态计算，再一次性应用；死亡不会取消另一方行动，双方同时归零为平局。日志中的伤害是规则计算的伤害，生命最低截断到 0。

双方选招后先同时揭晓，再显示伤害、格挡、能量变化。界面只用 `GetTime()` 推进展示，不阻塞窗口。AI 在每轮开始时预先锁定选择，没有访问玩家当前选择的接口。`--seed 42` 可复现同一标准库实现下的随机序列：

```powershell
.\build\Release\duel_client.exe --seed 42
```

AI：自身满能量时大招/攻击/防御为 60/25/15%；自身未满、对手已满时攻击/蓄力/防御为 50/40/10%；双方未满时攻击/防御/蓄力为 40/30/30%。

## 配置

源码配置在 `assets/config/game.json`，构建后复制到各可执行文件旁的 `assets/config/game.json`。程序默认按可执行文件路径定位配置，可以从项目根目录或构建目录启动。修改源码配置后需重新构建对应目标以复制，或直接指定配置：

```powershell
.\build\Release\duel_server.exe --config .\assets\config\game.json
.\build\Release\duel_client.exe --config .\assets\config\game.json
```

| 配置 | 默认值 | 含义 |
| --- | --- | --- |
| `rules.initialHp` | 5 | 1～20 |
| `rules.maxEnergy` / `ultimateCost` | 3 / 3 | 1～10，必须相等 |
| `rules.attackDamage` / `ultimateDamage` | 1 / 3 | 各 1～20 |
| `timing.selectionMs` | 30000 | 服务端选招时间；单机也使用此值 |
| `timing.revealMs` / `displayMs` | 700 / 1700 | 揭晓和结算展示时长 |
| `timing.waitingRoomMs` | 300000 | 未开局房间 5 分钟回收 |
| `timing.heartbeatMs` / `heartbeatTimeoutMs` | 3000 / 15000 | 心跳间隔与失联阈值；超时必须至少为间隔 3 倍 |
| `timing.connectTimeoutMs` | 5000 | 异步 DNS / 连接总超时 |
| `server.address` / `port` | `0.0.0.0` / 28765 | 监听所有 IPv4 网卡；端口 0 仅供测试随机分配 |
| `client.host` / `port` | `localhost` / 28765 | 菜单默认连接位置 |

缺失文件、非法 JSON、缺失字段、错误类型、越界数值会明确打印错误并以非零退出码结束，避免静默使用错误规则。联机 `match_start` 包含服务端规则；客户端本地规则只用于单机，不能覆盖联机权威值。心跳参数由服务端 `welcome` 协商。

## 代码结构与设计

```text
shared/
  Battle.*       纯 C++17 规则、类型、AI；无 JSON、网络、文件或 raylib 依赖
  Protocol.*     消息校验、序列化、长度前缀
  Config.*       配置加载与范围校验
  Connection.*   精确异步读、受限 FIFO 写队列、异常连接关闭
server/
  RoomService.*  可注入单调时间的房间/对局状态机
  main.cpp       单线程 io_context、连接身份、心跳和房间定时推进
client/
  LocalMatch.h   开始回合时生成 AI 行动，提交后才能得到结果
  Network.*      一个网络线程、互斥事件队列、RAII 停止和 join
  Game.*         客户端状态切换、键鼠输入、几何图形与日志
  main.cpp       配置、窗口生命周期、固定种子与截图入口
tests/
  Tests.cpp      规则、AI、房间与边界条件
  NetworkProbe.cpp 真实 C++ 客户端网络类双人五回合对局
  integration.py 进程/真实 socket 协议与异常测试
```

`duel_rules` 只链接标准库，客户端、服务端和测试共用同一个 `resolveRound`。错误返回 `Resolution{nullopt, error}`，输入状态为 const，不会部分修改。服务端身份来自 socket 连接，不接受客户端指定 `playerId`、伤害、生命或能量。服务端逐条处理请求，截止时间到达后先确定默认防御，再拒绝晚到行动，保证同一回合只结算一次。

详见 [状态图与架构](docs/ARCHITECTURE.md)、[协议说明](docs/PROTOCOL.md)。

## 测试与验收

```powershell
ctest --test-dir build -C Release --output-on-failure
# 独立重跑真实 TCP 集成测试，服务端由脚本自动启动/停止，不占用固定端口
python .\tests\integration.py --server .\build\Release\duel_server.exe --probe .\build\Release\network_probe.exe --config .\assets\config\game.json
```

CTest 注册 10 组单元测试和 1 组真实 TCP 集成测试。覆盖所有 16 种行动组合、能量合法性、同时死亡、AI 预先选招、房间号格式/唯一性、重复加入/提交、错误轮次、隐藏行动、仅一次结算、超时竞争、再战重置、拆包/粘包、超长/非法 JSON、断线、心跳和房间回收。

集成测试实际启动服务端，先让两个 C++ `Network` 客户端完成五回合平局，再用两个以上 Python socket 客户端验证协议异常和生命周期。不会用模拟对象代替网络验证。实际执行记录与手动清单见 [验收说明](docs/ACCEPTANCE.md)。

## 局域网与公网部署前提

局域网：服务端监听 `0.0.0.0`，客户端填服务端内网 IP，并允许所选 TCP 端口通过服务端防火墙。公网：需可访问的公网 IP/域名、入站端口放行；家用 NAT 可能需要端口转发，运营商 CGNAT 下通常需要可公网访问的主机或其他网络方案。项目不自动改变防火墙、不提供 NAT 穿透，也不包含公网部署凭据。

当前 TCP **没有传输加密、身份认证或密码学承诺协议**。房间号不是密码；知道服务器和房间号的人可以占用第二席位。服务端通过隐藏行动保证正常客户端之间选招保密，服务端运营者仍能看到双方行动。这是教学作品，不能描述为生产级安全系统。

## 已知限制

- 无断线重连、观战、账号或持久化；服务端退出会清空所有房间。断线提示是中止对局，不记为普通胜利。
- 单线程服务端；设置了 256 连接、128 房间、每连接每秒 120 条请求、128 条/256 KiB 待发送队列等保护，但未做公网压力或安全审计。
- 客户端倒计时从收到消息时开始，是近似值，网络延迟不影响服务端超时判定；窗口恢复时服从服务器当前阶段，可能缩短旧回合动画。
- 显示窗口固定 960×640、内置英文字体，无音效、外部美术、中文输入、手柄或分辨率设置。
- 固定种子在同一标准库实现下可复现，不保证不同编译器的分布采样序列完全一致。
- 配置加载错误输出到控制台；未加入独立的图形配置错误对话框。

## 可用于简历的描述

“使用 C++17、raylib、CMake 和独立 Asio 实现同时选招对战游戏，支持规则 AI 与六位房间号双人 TCP 对战；抽取公共纯规则模块，使用服务端权威状态机完成行动校验、同时结算、超时防御和双方同意再战；实现长度前缀协议、异步收发队列与客户端线程事件通信，并编写 CTest 单元测试和真实双客户端集成测试。”

不把未实测的并发量、延迟、跨平台支持或公网稳定性写成成果指标。
