# 阶段 2 开发记录：基础 C/S 网络通信与 Ping 协议

## 1. 本阶段目标

阶段 2 的目标是打通 Windows 客户端 `AVClient` 与 Ubuntu 服务端 `AVServer` 之间的最小 TCP 通信闭环。这个阶段只验证“能连接、能按协议发包、服务端能解析、能返回响应、客户端 UI 能显示结果”，不做文件上传、下载、远程媒体列表和远程播放。

之所以只做基础 C/S 通信，是因为阶段 1 刚完成本地播放和本地录制整合，下一步应该先验证客户端和服务端之间的网络通道是否可靠。如果一上来就做上传下载，会同时引入文件分片、磁盘读写、媒体目录、协议状态机、异常恢复等问题，排查难度会明显升高。

本阶段的核心验收是：

- `AVClient` 设置页可以输入服务器 IP 和端口。
- Windows 客户端可以连接 Ubuntu 服务端 `192.168.44.130:8000`。
- 客户端发送 `PING_RQ`。
- 服务端收到后打印日志并返回 `PING_RS`。
- 客户端网络线程收到响应后通过 Qt signal 通知 UI。
- UI 日志显示 `PING_RS`。

## 2. 本阶段完成的功能

- `AVClient` 新增网络模块目录 `AVClient/modules/network/`。
- 新增客户端协议头 `av_protocol.h`。
- 新增客户端 `TcpClient`，使用 Winsock 实现 TCP connect/send/recv。
- 新增客户端 `AVNetworkClient`，封装业务协议发送和响应解析。
- `AVClient.pro` 新增 network 模块源码和 `-lws2_32` 链接项。
- 设置页 `SettingsPage` 从阶段 1 占位页升级为网络测试页。
- 设置页新增 IP 输入框，默认 `192.168.44.130`。
- 设置页新增端口输入框，默认 `8000`。
- 设置页新增连接、断开、发送 Ping 按钮。
- 设置页新增连接状态、最近一次响应、网络日志区域。
- 新建独立 `AVServer` 目录。
- `AVServer` 实现最小阻塞式 TCP server。
- `AVServer` 支持默认端口 `8000`，也支持命令行指定端口。
- `AVServer` 实现 4 字节长度头 + 协议包体解析。
- 实现 `PING_RQ / PING_RS`。
- 实现测试版 `LOGIN_RQ / LOGIN_RS`，不接 MySQL。
- `AVServer` 不依赖 MySQL。
- 未实现上传、下载、远程媒体列表、远程播放。
- 阶段 1 的播放页和录制页没有改动核心逻辑。

## 3. 新增文件清单

### `AVClient/modules/network/av_protocol.h`

客户端协议定义文件。定义阶段 2 使用的协议号和包结构：

- `DEF_PACK_BASE`
- `DEF_PACK_PING_RQ`
- `DEF_PACK_PING_RS`
- `DEF_PACK_LOGIN_RQ`
- `DEF_PACK_LOGIN_RS`
- `STRU_PING_RQ`
- `STRU_PING_RS`
- `STRU_LOGIN_RQ`
- `STRU_LOGIN_RS`

设计上使用固定长度结构体，便于第一版协议快速验证。客户端和服务端各自保存一份同名协议头，内容保持一致。

### `AVClient/modules/network/TcpClient.h`

客户端 TCP 封装类声明。它负责底层 Winsock socket 生命周期、连接、断开、发送完整包、接收完整包和网络接收线程。

它不直接操作 UI，而是通过 Qt signal 抛出事件。

### `AVClient/modules/network/TcpClient.cpp`

客户端 TCP 封装类实现。主要功能：

- `WSAStartup()` 初始化 Winsock。
- 使用非阻塞 `connect + select` 实现连接超时。
- 连接成功后启动接收线程。
- 发送时自动加 4 字节包长度。
- 接收时先读 4 字节长度，再按长度读取完整包体。
- 连接断开或出错时发出 signal。

### `AVClient/modules/network/AVNetworkClient.h`

客户端网络业务封装类声明。它对 UI 提供更简单的接口：

- `connectToServer(ip, port)`
- `disconnectFromServer()`
- `sendPing()`
- `sendLogin(username, password)`

UI 不需要关心 Winsock 和 packet 细节。

### `AVClient/modules/network/AVNetworkClient.cpp`

客户端网络业务封装类实现。它负责：

- 调用 `TcpClient` 建立连接。
- 构造 `STRU_PING_RQ` 并发送。
- 构造 `STRU_LOGIN_RQ` 并发送。
- 解析收到的协议包类型。
- 收到 `PING_RS` 后发出 `pingResponse`。
- 通过 `logMessage` signal 通知设置页追加日志。

### `AVServer/include/av_protocol.h`

服务端协议定义文件。内容与客户端 `av_protocol.h` 保持一致，确保双方协议号和结构体大小一致。

### `AVServer/include/AVServer.h`

最小服务端类声明。定义：

- `start(port)`
- `readExact()`
- `sendPacket()`
- `handleClient()`
- `handlePacket()`

### `AVServer/src/main.cpp`

服务端入口文件。读取命令行端口参数，默认端口为 `8000`，创建 `AVServer` 并启动监听。

### `AVServer/src/AVServer.cpp`

最小 TCP server 实现。主要功能：

- 创建监听 socket。
- 绑定 `INADDR_ANY:port`。
- `listen()` 等待连接。
- `accept()` 客户端。
- 按 4 字节长度头读取协议包。
- 收到 `PING_RQ` 返回 `PING_RS`。
- 收到 `LOGIN_RQ` 返回固定成功的 `LOGIN_RS`。
- 打印关键日志。

### `AVServer/Makefile`

Ubuntu 服务端构建文件。运行 `make` 后生成可执行文件 `AVServer`。

## 4. 修改文件清单

### `.gitignore`

修改原因：

- 原规则忽略所有 `Makefile`，但阶段 2 需要提交 `AVServer/Makefile`，所以增加 `!AVServer/Makefile` 例外。
- `windeployqt` 会在 `AVClient/bin/` 下生成 Qt DLL、插件和翻译文件，这些属于构建/部署产物，所以增加 `AVClient/bin/` 忽略规则。

### `AVClient/AVClient.pro`

修改原因：

- 增加 Qt network 模块：`QT += ... network`。
- 增加网络模块源码：
  - `modules/network/AVNetworkClient.cpp`
  - `modules/network/TcpClient.cpp`
- 增加网络模块头文件：
  - `modules/network/AVNetworkClient.h`
  - `modules/network/TcpClient.h`
  - `modules/network/av_protocol.h`
- 增加 include 路径：`$$PWD/modules/network`。
- 增加 Winsock 链接项：`-lws2_32`。

### `AVClient/pages/SettingsPage.h`

修改原因：

- 阶段 1 的设置页只是占位。
- 阶段 2 需要在设置页添加网络连接测试 UI。

新增成员包括：

- `AVNetworkClient *m_networkClient`
- IP 和端口输入框。
- 连接、断开、Ping 按钮。
- 状态 Label。
- 最近响应 Label。
- 日志 QTextEdit。

### `AVClient/pages/SettingsPage.cpp`

修改原因：

- 创建网络测试 UI。
- 默认 IP 为 `192.168.44.130`。
- 默认端口为 `8000`。
- 点击 Connect 时调用 `AVNetworkClient::connectToServer()`。
- 点击 Disconnect 时调用 `disconnectFromServer()`。
- 点击 Send Ping 时调用 `sendPing()`。
- 使用 signal/slot 更新 UI 状态和日志。

### `AVClient/deploy/copy_runtime_dlls.ps1`

修改原因：

- 阶段 2 增加 Qt Network 依赖后，直接从 `AVClient/bin` 启动可能缺少 Qt runtime DLL。
- 脚本现在除了复制 FFmpeg/SDL/OpenCV DLL，还会复制 MinGW runtime，并尝试调用 `windeployqt --debug` 补齐 Qt debug DLL 和插件。

## 5. 核心类和核心函数说明

### `AVNetworkClient`

`AVNetworkClient` 是 UI 和底层 TCP 之间的业务适配层。设置页不直接操作 socket，而是调用 `AVNetworkClient`。

主要职责：

- 封装连接和断开。
- 构造 `PING_RQ` 和 `LOGIN_RQ`。
- 解析收到的 packet 类型。
- 把 `PING_RS`、`LOGIN_RS` 转换成 Qt signal。
- 将网络日志通过 `logMessage` 通知 UI。

关键函数：

- `connectToServer(const QString &ip, quint16 port)`：连接服务器。
- `disconnectFromServer()`：断开连接。
- `sendPing()`：发送 `PING_RQ`。
- `sendLogin()`：发送测试登录包。
- `onPacketReceived()`：解析服务端响应。

### `TcpClient`

`TcpClient` 是 Windows 客户端底层 TCP 封装，使用 Winsock 实现。

主要职责：

- `WSAStartup()` 和 `WSACleanup()`。
- 创建 socket。
- 连接服务器。
- 发送 4 字节长度头 + 包体。
- 接收 4 字节长度头 + 包体。
- 在独立线程中循环接收数据。
- 通过 signal 把连接、断开、收到 packet、错误通知上层。

关键函数：

- `connectToServer()`：使用非阻塞 connect 和 select 实现超时控制。
- `sendPacket()`：对业务包加长度头。
- `recvLoop()`：接收线程入口。
- `recvAll()`：确保读取指定字节数，解决半包问题。
- `sendAll()`：确保完整发送 frame。

### `av_protocol.h`

协议头定义了客户端和服务端共同理解的数据结构。阶段 2 使用简单固定结构：

```cpp
4 字节长度头 + 协议包体
```

包体第一个字段统一是 `PackType type`，用于区分协议类型。当前支持：

- `PING_RQ`
- `PING_RS`
- `LOGIN_RQ`
- `LOGIN_RS`

使用 4 字节长度头的原因是 TCP 是字节流协议，不保留应用层消息边界。接收端必须知道一个完整包有多长，才能从字节流里拆出完整消息。

### `AVServer`

`AVServer` 是阶段 2 新建的最小服务端，不依赖原始 `NetDisk-Server`，也不依赖 MySQL。

主要流程：

1. `socket()` 创建监听 socket。
2. `setsockopt(SO_REUSEADDR)` 允许快速重启。
3. `bind()` 绑定端口。
4. `listen()` 开始监听。
5. `accept()` 接收客户端连接。
6. `readExact()` 读取 4 字节长度头。
7. 按长度读取包体。
8. `handlePacket()` 根据协议号处理。
9. 收到 `PING_RQ` 返回 `PING_RS`。
10. 收到 `LOGIN_RQ` 返回固定成功 `LOGIN_RS`。

### UI 如何通过 signal/slot 更新网络状态

网络线程不直接操作 Qt 控件。`TcpClient` 在线程中收到数据后发出 `packetReceived(QByteArray)`，`AVNetworkClient` 解析协议后发出 `logMessage`、`pingResponse`、`connectedChanged` 等信号。`SettingsPage` 连接这些信号，并在主线程更新 Label 和 QTextEdit。

这样做可以避免跨线程直接访问 UI 控件导致的不稳定和崩溃。

## 6. 通信流程说明

```text
AVClient 点击 Connect
        |
        v
SettingsPage 调用 AVNetworkClient::connectToServer()
        |
        v
TcpClient 创建 Winsock socket
        |
        v
连接 Ubuntu AVServer 192.168.44.130:8000
        |
        v
连接成功后 signal 通知 SettingsPage 更新状态
        |
        v
AVClient 点击 Send Ping
        |
        v
AVNetworkClient 构造 STRU_PING_RQ
        |
        v
TcpClient 发送 4 字节长度头 + PING_RQ
        |
        v
AVServer 读取长度头和包体
        |
        v
AVServer 解析协议类型为 PING_RQ
        |
        v
AVServer 返回 4 字节长度头 + PING_RS
        |
        v
TcpClient 接收完整 PING_RS 包体
        |
        v
AVNetworkClient 解析 PING_RS
        |
        v
signal 通知 SettingsPage
        |
        v
设置页日志显示 received PING_RS
```

## 7. 构建方法

### Windows 客户端

项目根目录：

```text
D:\colin\project\AVProject
```

构建命令：

```powershell
cd D:\colin\project\AVProject\AVClient
mkdir build-debug
cd build-debug

$env:PATH='D:\Software\Qt\Tools\mingw730_32\bin;D:\Software\Qt\5.12.11\mingw73_32\bin;' + $env:PATH
D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe ..\AVClient.pro -spec win32-g++ "CONFIG+=debug" "CONFIG+=qml_debug"
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe -j4

cd D:\colin\project\AVProject
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\AVClient\deploy\copy_runtime_dlls.ps1
```

运行：

```powershell
D:\colin\project\AVProject\AVClient\bin\AVClient.exe
```

### Ubuntu 服务端

把项目同步到 Ubuntu 后执行：

```bash
cd AVProject/AVServer
make
./AVServer 8000
```

也可以使用默认端口：

```bash
./AVServer
```

本轮在当前 Windows 环境中已完成 `AVClient` 编译和启动冒烟验证。本机 WSL 没有安装 Linux 发行版，因此 `AVServer` 需要在 Ubuntu 虚拟机中执行 `make` 验证。

## 8. 测试方法

### Ubuntu 启动服务端

```bash
cd AVProject/AVServer
make
./AVServer 8000
```

预期服务端输出：

```text
server started
listening on port 8000
```

### Windows 启动客户端

```powershell
D:\colin\project\AVProject\AVClient\bin\AVClient.exe
```

进入设置页。

填写：

```text
Server IP: 192.168.44.130
Port: 8000
```

点击 `Connect`。

预期客户端：

- Status 显示 `Connected`。
- 日志显示 `connected`。

预期服务端：

```text
client connected: ...
```

点击 `Send Ping`。

预期服务端：

```text
received PING_RQ
sent PING_RS
```

预期客户端：

- 日志显示 `sent PING_RQ`。
- 日志显示 `received PING_RS: pong from AVServer`。
- Last response 显示 `pong from AVServer`。

点击 `Disconnect`。

预期客户端：

- Status 显示 `Disconnected`。
- 日志显示 `disconnected`。

预期服务端：

```text
client disconnected
```

## 9. 常见问题与排查

### Windows ping 不通 Ubuntu

可能原因：

- Ubuntu 虚拟机未启动。
- IP 地址变化。
- 虚拟机网络模式不正确。
- Windows 和 Ubuntu 不在同一网段。

排查方法：

- 在 Ubuntu 执行 `ip addr` 确认 IP 是否仍是 `192.168.44.130`。
- Windows 执行 `ping 192.168.44.130`。
- 检查虚拟机网络模式，优先使用能与宿主机互通的 NAT/桥接配置。

### 虚拟机网络模式问题

可能原因：

- NAT 模式未配置端口转发。
- 桥接模式下 IP 被 DHCP 改变。
- Host-only 模式只能宿主机访问，不能访问外网。

排查方法：

- 确认 Windows 能 ping 到 Ubuntu。
- 确认 Ubuntu 能 ping 到 Windows。
- 必要时固定 Ubuntu IP 或重新确认当前 IP 后填入设置页。

### Ubuntu 防火墙问题

可能原因：

- `ufw` 拦截 8000 端口。

排查方法：

```bash
sudo ufw status
sudo ufw allow 8000/tcp
```

### 服务端端口未监听

可能原因：

- `AVServer` 未启动。
- 启动端口不是 8000。
- bind 失败。

排查方法：

```bash
ss -lntp | grep 8000
```

确认服务端输出 `listening on port 8000`。

### 客户端连接被拒绝

可能原因：

- 服务端未启动。
- IP 或端口填错。
- 防火墙阻断。

排查方法：

- 先在 Ubuntu 启动 `./AVServer 8000`。
- 检查设置页 IP 和端口。
- 用 `telnet 192.168.44.130 8000` 或其他 TCP 工具测试端口连通性。

### 32-bit MinGW 与 Winsock 链接问题

可能原因：

- `.pro` 没有链接 `-lws2_32`。
- 使用了错误 Qt Kit。

排查方法：

- 检查 `AVClient.pro` 中 `LIBS += -lws2_32`。
- 使用 Qt 5.12.11 MinGW 7.3.0 32-bit。
- 重新执行 qmake，避免 Makefile 仍是旧配置。

### 客户端能连接但 Ping 没响应

可能原因：

- 服务端协议头和客户端协议头不一致。
- 服务端没有读到完整包。
- 客户端发送的协议号不匹配。

排查方法：

- 对比两个 `av_protocol.h`。
- 看服务端是否打印 `received PING_RQ`。
- 看客户端日志是否显示 `sent PING_RQ`。

### 阶段 1 播放/录制功能回退

可能原因：

- `AVClient.pro` 改动影响了原有模块编译。
- DLL 部署不完整。
- 设置页网络线程影响主线程。

排查方法：

- 先重新构建 `AVClient`。
- 执行 `copy_runtime_dlls.ps1`。
- 启动后分别测试播放页和录制页。
- 如果播放录制异常，优先对照阶段 1 的最后一次可用版本。

## 10. 本阶段未做内容

阶段 2 没有实现：

- 文件上传。
- 文件下载。
- 远程媒体列表。
- 远程播放。
- MySQL 用户系统。
- 服务端媒体目录管理。
- epoll + 线程池正式服务端。
- 自定义 TCP 边下边播。

这些内容会在后续阶段逐步实现。当前只验证最基础的 C/S 协议收发闭环。

## 11. 下一阶段建议

阶段 3 建议开始完善 `AVServer` 框架：

- 从最小阻塞式 server 过渡到更完整的服务端结构。
- 可以参考 `NetDisk-Server` 的 epoll + 线程池 + 协议分发思想。
- 先解除 MySQL 强依赖，保持服务端容易启动。
- 定义媒体列表协议。
- 设计上传和下载的分片协议。
- 增加服务端媒体目录，例如 `AVServer/media/`。
- 先做本地文件目录扫描，再考虑数据库。

## 12. 面试问答准备

### 1. 为什么使用 4 字节长度头？

因为 TCP 是字节流协议，不会保留应用层每次 `send()` 的边界。一次发送的数据可能被拆成多次接收，也可能多次发送的数据被合并接收。4 字节长度头可以告诉接收端当前包体有多长，接收端先读长度，再按长度读完整包体，这样就能解决粘包和半包问题。

### 2. TCP 为什么会有粘包和半包？

TCP 只保证字节流有序可靠，不保证消息边界。发送端调用两次 `send()`，接收端可能一次 `recv()` 全收到，这就是粘包；发送端一次 `send()` 很大的数据，接收端可能多次 `recv()` 才收完，这就是半包。所以应用层必须自己设计拆包规则。

### 3. 为什么网络线程不能直接操作 Qt UI？

Qt UI 控件应该只在主线程访问。网络接收线程如果直接修改 QLabel、QTextEdit 等控件，可能导致线程竞争、随机崩溃或 UI 状态异常。正确做法是网络线程发 signal，Qt 自动把跨线程信号投递到 UI 线程执行 slot。

### 4. 为什么阶段 2 只做 Ping/Pong？

Ping/Pong 是最小协议闭环，可以验证连接、发送、接收、拆包、协议号解析、UI 日志更新。如果这个闭环不稳定，上传下载一定更不稳定。先把最小链路跑通，可以降低后续开发风险。

### 5. 客户端和服务端如何保证协议一致？

双方需要使用一致的协议号、结构体字段、字段长度和对齐方式。本阶段客户端和服务端都有 `av_protocol.h`，内容保持一致，并使用 `#pragma pack(push, 1)` 固定结构体对齐。后续更好的方式是把协议头抽到公共目录，避免两份文件手动同步。

### 6. 为什么服务端暂时不接 MySQL？

阶段 2 的目标是网络通信，不是用户系统。接入 MySQL 会引入数据库安装、账号密码、表结构、SQL 错误等额外变量，可能导致服务端还没验证网络就启动失败。所以本阶段 `LOGIN_RS` 只是固定返回成功，用于验证协议收发。

### 7. Windows 客户端和 Ubuntu 服务端通信要注意什么？

首先要保证网络可达，Windows 能 ping 到 Ubuntu。其次要保证 Ubuntu 服务端监听的是 `0.0.0.0:8000` 或对应网卡 IP，而不是只监听 localhost。还要检查防火墙、虚拟机网络模式和端口是否一致。协议结构也要注意字节序和结构体对齐，本阶段默认双方都是 x86 小端环境。

### 8. 如果连接失败应该如何排查？

先确认 Ubuntu 服务端是否启动，并输出 `listening on port 8000`。再确认 IP 是否是 `192.168.44.130`，Windows 是否能 ping 通。然后在 Ubuntu 上用 `ss -lntp | grep 8000` 看端口是否监听。最后检查 Windows 客户端设置页 IP、端口是否填写正确，以及 Ubuntu 防火墙是否放行 8000。

### 9. 为什么客户端还要封装 `AVNetworkClient`，而不是 UI 直接用 `TcpClient`？

`TcpClient` 只关心 socket 和字节流，属于网络传输层。`AVNetworkClient` 关心 `PING_RQ`、`PING_RS`、`LOGIN_RQ` 这些业务协议，属于业务适配层。UI 只调用 `sendPing()`，不用知道包长度、结构体和 Winsock 细节，这样层次更清楚，后续扩展媒体列表、上传下载也更容易。

### 10. 当前最小阻塞式 `AVServer` 有什么局限？

它一次主要处理一个客户端连接，没有 epoll，没有线程池，也没有复杂并发处理。优点是简单、稳定、容易验证协议；缺点是不适合高并发和正式媒体服务。阶段 3 可以参考 `NetDisk-Server` 的 epoll + 线程池框架，把协议分发和并发能力补上。
