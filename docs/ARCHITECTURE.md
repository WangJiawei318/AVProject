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
| `RemoteMediaPage` | 列表、上传、下载、进度 | `AVNetworkClient` |
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

`AVNetworkClient` 解析业务包，再发出 Ping、列表、上传、下载等 Qt 信号。Qt 的跨线程 queued connection 让 UI 槽函数回到对象所属的主线程执行，因此接收线程不直接操作控件。

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

客户端 `recvAll()` 和服务端 `readExact()` 循环读取，解决 TCP 半包。下一条消息的长度头又提供了消息边界，解决粘包。

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
  -> 循环 UPLOAD_BLOCK
  -> UPLOAD_FINISH
  -> 刷新媒体列表
```

`RemoteMediaPage` 任意时刻只发送一个未确认分片。收到 `UPLOAD_BLOCK_RS` 后，才推进 offset 并发送下一块。

### 9.2 UploadManager

服务端 `UploadManager` 保存：

- `upload_id`；
- 原文件名和扩展名；
- 预期大小和已接收大小；
- `temp/<upload_id>.part` 路径。

它要求每块 offset 等于当前已接收大小。完成时比较声明大小、累计大小和磁盘实际大小，然后把临时文件移动到 `media/`。

### 9.3 temp 的意义

上传中断时，半成品不会出现在媒体列表。只有 FINISH 成功的文件才进入 `media/`，这相当于一个简单的提交边界。

## 10. 下载模块

### 10.1 下载状态

```text
选择远程文件
  -> DOWNLOAD_INIT
  -> 创建 cache/*.part
  -> 循环 DOWNLOAD_BLOCK
  -> DOWNLOAD_FINISH
  -> .part 改为正式文件
  -> 可选：切换播放页
```

客户端下载进度以“已经校验并写入磁盘的字节数”为准。

### 10.2 DownloadManager

服务端 `DownloadManager` 每次请求都验证：

- 文件名没有路径分隔符和 `..`；
- 文件位于固定 `media/`；
- 文件是普通文件且后缀受支持；
- offset 和 request size 合法。

随后只读取当前请求的最多 64 KB。

### 10.3 cache 和 .part

下载数据先写入 `AVClient/cache/<name>.part`。全部字节写完且 FINISH 成功后，才替换正式 cache 文件。断线时删除 `.part`，因此播放器不会误打开明显不完整的下载。

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
      -> socket / bind / listen
      -> accept 一个客户端
      -> handleClient()
          -> readExact(length)
          -> readExact(body)
          -> handlePacket()
      -> 客户端断开后继续 accept
```

`handlePacket()` 根据 `PackType` 使用 switch 分发到 Ping、列表、上传和下载处理函数。

### 14.2 当前并发能力

当前服务器在 `accept()` 后同步执行 `handleClient()`。一个客户端未断开时，主循环不会 accept 第二个客户端。因此它适合阶段验证和单客户端演示，不适合直接描述为高并发服务器。

### 14.3 与 NetDisk-Server 的关系

原始 `NetDisk-Server` 包含 epoll、线程池、协议分发和 MySQL 示例。当前 `AVServer` 借鉴了长度帧和协议分发思想，但为了降低依赖与整合风险，先实现了独立的最小服务器。

未来可把当前 `MediaManager`、`UploadManager` 和 `DownloadManager` 作为业务层，接入 epoll 网络层与线程池。

## 15. 关键设计取舍

| 取舍 | 当前选择 | 原因 |
| --- | --- | --- |
| 服务端并发 | 阻塞式单连接 | 先验证协议和文件闭环 |
| 媒体索引 | 扫描目录 | 不依赖 MySQL |
| 传输方式 | 64 KB 串行 ACK | 状态简单、内存固定 |
| 上传落盘 | temp 后提交 | 不暴露半成品 |
| 下载落盘 | cache `.part` 后改名 | 不播放半成品 |
| 远程播放 | 完整下载后本地播放 | 复用播放器，不改 AVIO |
| 页面通信 | signal/slot + MainWindow 中介 | 降低耦合 |

## 16. 当前限制与演进方向

当前限制：

- 单连接阻塞服务器；
- 协议使用主机字节序和 packed struct，跨架构能力有限；
- 没有认证、权限、配额和 TLS；
- 没有任务持久化、断点和哈希；
- 没有取消、超时和自动重试；
- 目录扫描没有分页。

建议演进顺序：

1. 为协议增加版本、网络字节序和明确整数编码。
2. 为上传下载增加 task id、取消、超时和哈希。
3. 将服务端替换为 epoll + 工作线程模型。
4. 增加用户认证和可选数据库索引。
5. 最后评估自定义 AVIO、HTTP/HLS 或对象存储。

