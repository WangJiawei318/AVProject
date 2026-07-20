# AVProject 系统架构说明

## 1. 阅读目标

这份文档从“小白第一次打开项目”的角度解释：

- 系统由哪些程序组成；
- 一次播放、录制、上传和下载分别经过哪些类；
- Qt UI、工作线程、TCP 和文件系统如何配合；
- 为什么某些对象由 `MainWindow` 统一管理；
- 当前架构已经做到什么，还没有做到什么。

## 2. 总体架构

AVProject 是典型的客户端/服务端系统。

```text
┌──────────────── Windows ────────────────┐
│ AVClient                               │
│                                        │
│ Player  Recorder  Remote  Settings     │
│    │       │        │        │         │
│    └───────┴────────┴────────┘         │
│              MainWindow                │
│                  │                     │
│          AVNetworkClient               │
│                  │                     │
│             TcpClient                  │
└──────────────────┼─────────────────────┘
                   │ TCP
                   │ 4 字节包长 + 协议包体
┌──────────────────┼─────────────────────┐
│ Ubuntu           ▼                     │
│               AVServer                 │
│          ┌───────┼────────┐            │
│          ▼       ▼        ▼            │
│ MediaManager UploadManager DownloadManager
│          │       │        │            │
│        media/  temp/     media/         │
│                ├─ *.part                │
│                └─ tasks/*.task          │
└────────────────────────────────────────┘
```

客户端负责交互和音视频能力，服务端负责网络请求和媒体文件管理。两者不共享内存，只通过 TCP 自定义协议交换数据。

## 3. 代码来源与主线边界

项目保留四个原始学习工程：

```text
MediaPlayer       -> AVClient/modules/player
VideoRecorder     -> AVClient/modules/recorder
NetDisk-Client    -> AVClient/modules/network 的设计参考
NetDisk-Server    -> Linux 服务端、epoll、线程池的设计参考
```

实际运行主线是：

```text
AVClient/ + AVServer/
```

四个原始工程没有被删除。客户端采用复制核心模块的方式整合，因此对 `AVClient/modules` 的修改不会反向破坏原始播放器和录屏器。

## 4. Windows 客户端架构

### 4.1 页面层

`MainWindow` 创建一个 `QTabWidget`，包含：

| 页面 | 作用 | 核心对象 |
| --- | --- | --- |
| `PlayerPage` | 本地文件、URL 和下载缓存播放 | `PlayerDialog` |
| `RecorderPage` | 桌面、摄像头、麦克风录制 | `RecorderDialog` |
| `RemoteMediaPage` | 列表、可恢复上传/下载、进度 | `AVNetworkClient`、`UploadTaskStore`、`DownloadTaskStore` |
| `SettingsPage` | IP、端口、连接、断开、Ping | `AVNetworkClient` |

`PlayerPage` 和 `RecorderPage` 是适配层：它们把原来独立窗口形式的 Dialog 嵌入统一客户端。

### 4.2 共享服务层

`MainWindow` 只创建一个 `AVNetworkClient`：

```text
MainWindow
  └── AVNetworkClient
       └── TcpClient
```

`SettingsPage` 和 `RemoteMediaPage` 拿到同一个指针。这样整个客户端只有一个 TCP 连接和一个接收线程。

### 4.3 网络线程与 UI

`TcpClient` 使用 Winsock 建立连接，并启动一个 `std::thread` 执行接收循环。接收线程只负责：

1. 读满 4 字节长度；
2. 校验包长；
3. 读满包体；
4. 发出 `packetReceived(QByteArray)`。

`AVNetworkClient` 解析业务包，再发出 Ping、列表、上传、恢复、下载等 Qt 信号。Qt 的跨线程 queued connection 让 UI 槽函数回到对象所属的主线程执行，因此接收线程不直接操作控件。

## 5. 播放模块

### 5.1 类关系

```text
PlayerPage
  └── PlayerDialog
       └── VideoPlayer : QThread
            ├── PacketQueue audioq
            ├── PacketQueue videoq
            ├── SDL audio callback
            ├── SDL video decode thread / timer
            └── FFmpeg contexts

VideoPlayer -> QImage signal -> PlayerDialog -> MyOpenGLWidget
```

### 5.2 播放流程

1. `PlayerDialog` 获取本地路径或 URL。
2. `VideoPlayer::setFileName()` 保存输入。
3. `VideoPlayer::start()` 启动读取线程。
4. FFmpeg 打开输入，查找音频流和视频流。
5. `av_read_frame()` 连续读取压缩 packet。
6. 音频 packet 进入 `audioq`，视频 packet 进入 `videoq`。
7. SDL 音频回调从 `audioq` 取包、解码、重采样并播放。
8. 视频线程从 `videoq` 取包、解码、转换成图像。
9. 图像通过 Qt signal 交给 `MyOpenGLWidget` 显示。

### 5.3 音视频同步

项目以音频时钟为主要参考：

- 音频实际送入 SDL 后推进 `audio_clock`；
- 视频帧根据时间戳计算 `video_clock`；
- 视频线程比较两者，决定等待或尽快显示；
- 无音频的纯视频使用 SDL timer 控制帧节奏。

### 5.4 seek

UI 只设置 seek 请求和目标位置。读取线程执行 FFmpeg seek，并向音视频队列放入 flush 标记，使解码器丢弃旧位置缓存，避免跳转后继续播放旧帧。

### 5.5 下载文件如何进入播放器

阶段 5 新增了薄接口：

```text
PlayerPage::playLocalFile()
  -> PlayerDialog::playLocalFile()
  -> VideoPlayer::setFileName()
  -> VideoPlayer::start()
```

这个接口没有改变解码、同步和渲染核心。

## 6. 录制模块

### 6.1 类关系

```text
RecorderPage
  └── RecorderDialog
       └── SaveVideoFileThread
            ├── PicInPic_Read
            │    ├── 桌面采集
            │    └── 摄像头画中画
            ├── Audio_Read / QAudioInput
            └── FFmpeg 编码与 FLV 封装
```

### 6.2 录制流程

1. `RecorderDialog` 构造输出参数。
2. 默认输出为 `AVClient/bin/recordings/record_时间.flv`。
3. `PicInPic_Read` 抓取主屏幕，并采集摄像头画面。
4. 桌面和摄像头合成后转换为编码需要的 YUV 数据。
5. `Audio_Read` 通过 `QAudioInput` 获取麦克风 PCM。
6. 视频帧和音频帧分别进入录制模块队列。
7. `SaveVideoFileThread` 调用 FFmpeg 编码。
8. 视频编码为 H.264，音频编码为 AAC。
9. 编码 packet 写入 FLV 容器。
10. 停止时写 trailer、关闭编码器和输出文件。

### 6.3 为什么录制放在线程中

采集、颜色转换、编码和磁盘写入都可能耗时。如果全放在 UI 线程，按钮、窗口和预览会卡顿。`SaveVideoFileThread` 将持续编码流程与 UI 事件循环分离。

## 7. 网络通信模块

### 7.1 分层

```text
页面
  ↓ 业务调用 / Qt signal
AVNetworkClient
  ↓ 业务结构序列化
TcpClient
  ↓ 长度帧与 send/recv
Winsock TCP
```

`TcpClient` 不理解媒体列表、上传或下载；`AVNetworkClient` 不负责 socket 细节；页面不解析原始字节。这种分层降低了相互影响。

### 7.2 数据边界

所有消息使用：

```text
[int32_t packLen][packLen 字节业务包体]
```

客户端 `recvAll()` 循环读取长度头和包体；服务端把 non-blocking `recv()` 得到的字节追加到每连接 `receiveBuffer` 并增量解析。两种实现都依靠长度头恢复消息边界，从而处理半包和粘包。

## 8. 远程媒体库模块

### 8.1 MediaManager

`MediaManager`：

- 确保 `media/` 存在；
- 扫描普通文件；
- 过滤支持的媒体后缀；
- 获取文件大小和修改时间；
- 生成 `filename|size|mtime|extension` 文本列表。

客户端收到列表后按换行拆记录、按 `|` 拆字段，并填入 `QTableWidget`。

### 8.2 为什么第一版不用数据库

目录扫描已经能完成最小媒体列表，部署时不需要数据库服务、账号、表结构和迁移。缺点是大目录效率、分页、标签和复杂搜索能力有限。数据库适合在基本闭环稳定后引入。

## 9. 上传模块

### 9.1 上传状态

```text
选择文件
  -> UPLOAD_INIT
  -> 保存客户端任务状态
  -> 循环 UPLOAD_BLOCK
  -> 每个 ACK 更新 confirmed_offset
  -> UPLOAD_FINISH
  -> 刷新媒体列表

断线或进程退出
  -> 客户端保留 transfer_state
  -> 服务端保留 temp/*.part 与 temp/tasks/*.task
  -> 重连后 UPLOAD_RESUME
  -> 从服务端 resume_offset 继续 BLOCK
```

`RemoteMediaPage` 任意时刻只发送一个未确认分片。收到 `UPLOAD_BLOCK_RS` 后，才推进 offset 并发送下一块。

### 9.2 UploadManager

服务端 `UploadManager` 保存：

- `transfer_id` 和随机 `resume_token`；
- 原文件名、最终文件名和扩展名；
- 预期大小和服务端已可靠写入大小；
- `temp/<transfer_id>.part` 路径；
- 创建、更新时间和任务状态；
- 仅在内存中存在的 `active_owner_connection_id`。

每个任务还对应 `temp/tasks/<transfer_id>.task`。新建任务和每次分片落盘后都会把元数据先写到临时元数据文件，再 `rename` 覆盖正式任务文件。服务器重启时扫描该目录，将未完成任务重新装入内存。

写块时要求 offset 严格连续。offset 小于已确认位置的完整重复块不会再次写入，服务端只回送当前确认偏移；offset 大于当前位置则拒绝。完成时比较声明大小、内存累计大小和 `.part` 实际大小，再把临时文件移动到 `media/` 并删除任务元数据。

### 9.3 temp 的意义

上传中断时，半成品不会出现在媒体列表，但会作为可恢复状态保留。只有 FINISH 成功的文件才进入 `media/`，这相当于一个简单的提交边界。未绑定活动连接且 72 小时没有更新的 uploading 任务会被过期清理。

### 9.4 客户端 UploadTaskStore

客户端把每个未完成上传保存为 `AVClient/transfer_state/<transfer_id>.upload.json`。状态包含本地路径、文件大小、最后修改时间、服务器地址、任务 ID、token、确认偏移和状态。`QSaveFile` 负责临时文件写入与原子提交。

AVClient 重启后会重新加载这些文件并显示在“未完成上传任务”表格。用户点击恢复时，客户端先验证本地文件仍存在，大小和最后修改时间未变化，并确认当前连接地址与任务记录一致；服务端验证 token 后返回真实 `resume_offset`，客户端再 seek 到该位置继续发送。token 不显示在普通 UI 日志中。

### 9.5 任务身份与连接绑定

`transfer_id` 用于定位任务，`resume_token` 用于证明调用方持有恢复凭据。当前还没有用户系统，所以 token 是临时能力凭据，不等同于账号权限。

socket fd 会在重连后变化，还可能被内核复用，服务端重启后也完全失效，因此不能作为永久任务身份。`active_owner_connection_id` 只表示“当前哪条连接正在操作此任务”：创建或恢复成功时绑定，连接关闭时解绑，同一时刻不允许另一连接恢复；解绑不会删除任务、元数据或 `.part`。connectionId 也不会被持久化，永久恢复凭证仍是 transfer ID 与 resume token。

## 10. 下载模块

### 10.1 下载状态

```text
选择远程文件
  -> DOWNLOAD_INIT(offset=0)
  -> 保存 size、mtime 和下载任务
  -> 创建 cache/*.part
  -> 循环 DOWNLOAD_BLOCK
  -> 每块落盘后更新 confirmed_offset
  -> DOWNLOAD_FINISH
  -> .part 改为正式文件
  -> 可选：切换播放页

断线或客户端退出
  -> 保留 .part 和 transfer_state/*.download.json
  -> 重连后计算 safe_offset
  -> DOWNLOAD_INIT(offset=safe_offset, old size, old mtime)
  -> 从 accepted_offset 继续 DOWNLOAD_BLOCK
```

客户端下载进度以“已经校验并写入磁盘的字节数”为准。

### 10.2 DownloadManager

服务端 `DownloadManager` 每次请求都验证：

- 文件名没有路径分隔符和 `..`；
- 文件位于固定 `media/`；
- 文件是普通文件且后缀受支持；
- 恢复时文件大小和修改时间仍与初次 INIT 一致；
- resume offset 不超过当前文件大小；
- offset 和 request size 合法。

随后只读取当前请求的最多 64 KB。`DownloadManager` 不保存下载任务、token 或文件游标，每个 BLOCK 都按 `filename + offset` 独立读取，因此服务端重启后无需恢复下载内存状态。

### 10.3 cache 和 .part

下载数据先写入 `AVClient/cache/<name>.part`。全部字节写完、实际大小正确且 FINISH 成功后，才替换正式 cache 文件。断线时保留 `.part` 供恢复，但播放器仍只打开正式文件名。

### 10.4 DownloadTaskStore 与安全偏移

每个未完成下载保存为 `AVClient/transfer_state/<task_id>.download.json`，包含远程文件名、cache 路径、服务器地址、文件大小、修改时间、确认偏移、是否下载后播放和任务状态。`QSaveFile` 通过临时文件加原子提交更新小型 JSON。

恢复前，客户端用 `min(confirmed_offset, .part 实际大小)` 计算 `safe_offset`。如果 `.part` 更大就截断未确认尾部，如果更小就降低状态偏移。服务端重新校验 size/mtime 后返回 `accepted_offset`，客户端以该值截断、seek 并继续。size/mtime 只能防止常见误拼接，不能识别“内容变化但大小和 mtime 恰好相同”，因此当前仍没有强哈希完整性保证。

## 11. 页面交互关系

```text
SettingsPage
   │ connect / disconnect / ping
   ▼
AVNetworkClient
   ▲
   │ list / upload / download
RemoteMediaPage
   │
   │ requestPlayLocalFile(path)
   ▼
MainWindow
   │ switch tab + playLocalFile(path)
   ▼
PlayerPage
```

页面之间不互相寻找控件，也不直接操作彼此内部对象。

## 12. 为什么 MainWindow 共享 AVNetworkClient

如果 Settings 和 Remote Media 各创建一个网络客户端，会出现：

- 两条不同 TCP 连接；
- 设置页显示的状态与媒体页使用的连接不一致；
- 两个接收线程和两套断线处理；
- 重复登录、重复资源和更难排错。

`MainWindow` 的生命周期覆盖所有页面，因此由它创建唯一网络对象，并把同一指针注入需要的页面，所有页面看到的是同一连接状态。

## 13. 为什么 MainWindow 转发播放请求

`RemoteMediaPage` 的职责是下载，不应知道标签页顺序和播放器内部结构。它只发出“请播放这个本地路径”的业务信号。

`MainWindow` 本来就负责页面组织，因此由它：

1. 切换到 Player 标签；
2. 调用 `PlayerPage::playLocalFile()`。

这样做降低页面耦合。将来把标签页换成侧边栏和 `QStackedWidget` 时，只需改 MainWindow。

## 14. Ubuntu 服务端架构

### 14.1 当前实现

```text
main()
  -> AVServer::start(8000)
      -> EpollServer::start(8000)
          -> 创建 non-blocking listen socket
          -> bind / listen / epoll_create1
          -> 创建 eventfd 和 4 个核心 worker
          -> epoll_wait LT 事件循环
              -> accept 新连接到 EAGAIN
              -> 为每个 fd 创建 connectionId + ConnectionContext
              -> EPOLLIN: recv 到 EAGAIN，增量解析长度帧
              -> Ping 等轻量包直接生成响应
              -> 文件业务提交有界 ThreadPool
                    -> ProtocolDispatcher / Manager / 文件 I/O
                    -> completionQueue
                    -> eventfd 唤醒 Reactor
              -> Reactor 校验 connectionId 后加入 sendQueue
              -> EPOLLOUT: 从 sendOffset 继续发送
              -> 错误或断开: 只清理当前 fd
```

`AVServer` 现在是很薄的启动外观，网络事件由 `EpollServer` 负责，完整协议包由 `ProtocolDispatcher` 按 `PackType` 分发到 Ping、列表、上传和下载处理函数。业务层只返回响应包体，不直接调用 `send()`。

### 14.2 当前并发能力

当前服务器采用单线程 Reactor + 有界动态业务线程池。一个 epoll 实例同时关注 listen fd、eventfd 和所有 client fd。每个连接都有独立 `ConnectionContext`，因此某个连接等待网络数据、发生半包或发送暂时不可写时，不会阻止服务器处理其他已就绪连接。

Reactor 独占 socket I/O、epoll_ctl、连接和发送队列；4 至 8 个 worker 并行执行媒体目录扫描、上传元数据与文件写入、下载文件读取和业务响应生成。任务队列上限 256，非核心 worker 空闲 60 秒退出。文件 I/O 仍是同步调用，只是从 Reactor 移到 worker，因此它改善了连接响应隔离，但不是异步 I/O 或生产级无限并发。

### 14.3 ConnectionContext

每个 client fd 对应一个 `ConnectionContext`，保存：

- 对端 IP 和端口；
- 单调递增且不复用的 `connectionId`；
- 独立 `receiveBuffer`；
- 独立 `sendQueue`、当前发送项及 offset；
- 已排队但尚未发送的字节数；
- 最后活动时间与连接状态。
- `businessTaskInFlight`，限制同连接最多一个业务任务在线程池执行。

接收时先把任意数量的字节追加到 `receiveBuffer`。缓冲区不足 4 字节时继续等待；读到包长后先校验范围，数据不足一个完整包时仍继续等待。当前长度头沿用主机字节序，最大包体 256 KB，单连接接收缓冲上限 4 MB。业务任务在途时仍继续 recv，但暂停分发后续完整帧；completion 到达后再从原缓冲顺序继续。

响应先被封装成“4 字节长度 + 包体”并进入该连接的发送队列。服务器立即尝试 `send()`；如果只写出一部分，就保存 offset 并注册 `EPOLLOUT`。队列清空后取消 `EPOLLOUT`，避免 socket 长期可写导致事件循环空转。单连接队列上限为 4 MB，慢客户端持续不读时会被关闭，防止无限占用内存。

### 14.4 协议与业务层

`ProtocolDispatcher` 持有 `MediaManager`、`UploadManager` 和 `DownloadManager`。它接收已经去掉长度头的完整包体，校验具体结构大小，调用业务管理器，再把一个或多个响应包体交还给网络层。这样 `EpollServer` 不理解媒体业务，业务管理器也不依赖 epoll。

PING/LOGIN 可由 Reactor 直接调用 Dispatcher；媒体列表及上传、下载协议由 worker 调用。Dispatcher 不直接 send、不调用 epoll_ctl，也不接收 ConnectionContext。

上传任务的持久身份仍是 `transfer_id + resume_token`，当前会话排他绑定改为 `active_owner_connection_id`。UploadManager 用一把 mutex 保护任务 map、偏移、重名、绑定和过期清理。下载继续使用 `filename + offset + request_size` 无状态读取，不建立服务端下载任务，也不共享文件游标。

### 14.5 completionQueue 与 eventfd

worker 生成 `CompletedTask` 后，在 mutex 下移动到 completion queue，再向唯一 eventfd 写 1。Reactor 被 epoll 唤醒，批量 swap 出结果，按 connectionId 查找当前连接。连接已关闭或 fd 已复用时丢弃 completion；连接仍有效时才清除 in-flight、排队响应并继续解析缓存。

工作线程不持有 ConnectionContext 指针，因此断开不会产生悬空访问。断开时和迟到 completion 被丢弃时都会尝试解除上传会话绑定，覆盖“INIT 在断开回调之后才完成”的竞态。

### 14.6 与 NetDisk-Server 的关系

原始 `NetDisk-Server` 包含 epoll、线程池、协议分发和 MySQL 示例。主线 `AVServer` 吸收了 Reactor、生产者消费者和协议分层思想，但线程池、completion/eventfd 与连接身份按当前媒体业务重新实现，没有照搬网盘业务或 MySQL。

主线保持“Reactor 拥有连接，worker 拥有业务调用”的单向边界。后续若增加更复杂调度，应继续通过不可变任务和 completion 回投，而不是让 worker 直接操作连接。

## 15. 关键设计取舍

| 取舍 | 当前选择 | 原因 |
| --- | --- | --- |
| 服务端并发 | epoll LT Reactor + 4 至 8 个 worker | 网络状态串行、业务可并行 |
| 任务队列 | 有界 256 | 过载返回 server busy，避免无限内存增长 |
| 完成通知 | completion queue + 单 eventfd | worker 不直接操作 socket 或 epoll |
| 同连接顺序 | 一个 business task in-flight | 保证上传分片和响应顺序 |
| 文件 I/O | worker 内同步 64 KB 读写 | 隔离 Reactor，但尚非异步 I/O |
| 媒体索引 | 扫描目录 | 不依赖 MySQL |
| 传输方式 | 64 KB 串行 ACK | 状态简单、内存固定 |
| 上传落盘 | temp 后提交 | 不暴露半成品 |
| 上传恢复身份 | transfer_id + resume_token | fd 变化后仍可恢复，并阻止只猜 ID 的客户端 |
| 上传状态 | 客户端 JSON + 服务端键值元数据 | 不引入数据库即可跨进程恢复 |
| 下载恢复状态 | 仅客户端 JSON + `.part` | 服务端按 offset 无状态读取，重启无需恢复任务 |
| 下载版本判断 | 文件大小 + 修改时间 | 低成本发现常见变化，但不等同于内容哈希 |
| 下载落盘 | cache `.part` 后改名 | 不播放半成品 |
| 远程播放 | 完整下载后本地播放 | 复用播放器，不改 AVIO |
| 页面通信 | signal/slot + MainWindow 中介 | 降低耦合 |

## 16. 当前限制与演进方向

当前限制：

- 已有有界动态业务线程池，但扩缩容只依据 pending/idle，缺少生产级负载指标；
- 文件 I/O 仍是 worker 中的同步调用；UploadManager 粗粒度锁会限制多上传并行；
- 协议使用主机字节序和 packed struct，跨架构能力有限；
- 没有认证、权限、配额和 TLS；
- 已支持上传和下载断点续传；上传状态在客户端和服务端持久化，下载状态仅在客户端持久化；
- 当前 token 不是真正用户权限，且没有 TLS；
- 只校验大小、偏移和修改时间等元数据，没有 SHA-256 强内容校验；
- 没有通用取消和自动重试；客户端“放弃任务”只删除本地记录；
- 没有任务优先级、工作窃取和客户端同连接多业务并行；
- FINISH 已提交但客户端尚未收到响应时崩溃，可能留下需要人工放弃的本地状态记录；
- 目录扫描没有分页。

建议演进顺序：

1. 为协议增加版本、网络字节序和明确整数编码。
2. 为传输增加通用取消、稳定文件版本标识与 SHA-256 校验。
3. 完善线程池任务指标、超时治理和 UploadManager 细粒度并发控制。
4. 增加用户认证和可选数据库索引。
5. 最后评估自定义 AVIO、HTTP/HLS 或对象存储。
