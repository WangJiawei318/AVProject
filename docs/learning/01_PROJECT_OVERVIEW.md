# AVProject 项目总览

## 1. 项目解决的问题

AVProject 将本地音视频能力与 C/S 媒体管理串成一个可演示闭环：Windows 客户端完成播放、录制、上传、下载和任务恢复；Ubuntu 服务端完成 TCP 接入、媒体目录管理、文件分发及上传任务持久化。它不是流媒体平台，远程播放采用“先下载到本地 cache，再调用本地播放器”。

## 2. 两端职责

- **AVClient**：Qt Widgets UI；FFmpeg 解封装、解码、编码与封装；SDL 音频输出；OpenGL 视频显示；OpenCV 摄像头采集；Winsock TCP；本地上传/下载任务状态。
- **AVServer**：Linux socket、epoll LT Reactor、动态业务线程池、MySQL 用户认证与媒体索引、上传任务与 `.part/.task` 管理、无状态分片下载。

## 3. 从四个参考项目到主线

| 原项目 | 主线吸收内容 | 当前位置 |
|---|---|---|
| MediaPlayer | FFmpeg 解码、SDL 音频、音视频同步、seek、OpenGL 渲染 | `AVClient/modules/player/` |
| VideoRecorder | 桌面/摄像头/麦克风采集、H.264/AAC 编码、FLV 封装 | `AVClient/modules/recorder/` |
| NetDisk-Client | TCP 客户端、协议结构体、信号转发思路 | `AVClient/modules/network/` |
| NetDisk-Server | Linux C/S、epoll、协议分发、线程池思路 | `AVServer/` |

原始四个项目仍保留为参考，主线通过复制后小范围适配形成，不依赖直接修改原项目。

## 4. 总体架构

```text
AVClient
├── MainWindow（页面组合、共享网络对象、跨页面转发）
├── PlayerPage -> PlayerDialog -> VideoPlayer -> PacketQueue/MyOpenGLWidget
├── RecorderPage -> RecorderDialog -> SaveVideoFileThread
│                                  -> PicInPic_Read/Audio_Read
├── RemoteMediaPage -> UploadTaskStore/DownloadTaskStore
├── SettingsPage
└── AVNetworkClient -> TcpClient
                         │ 4 字节长度 + 协议包体 / TCP
AVServer                 ↓
├── AVServer -> EpollServer（LT Reactor）
├── ConnectionContext（每连接收发状态）
├── ThreadPool（4 核心、最多 8、队列 256）
├── ProtocolDispatcher
├── MediaManager
├── UploadManager（服务端有状态、可恢复）
└── DownloadManager（服务端无状态、按 offset 读取）
```

## 5. 完整业务闭环

1. 客户端本地播放或录制 FLV。
2. 设置页连接 `AVServer`，Ping 验证连接。
3. 远程媒体页获取 `media/` 列表。
4. 选择本地媒体，以 64 KB 串行分片上传；中断后凭 `transfer_id + resume_token` 恢复。
5. 服务端 FINISH 校验大小后将 `.part` 重命名到 `media/`。
6. 客户端选择远程文件，以 64 KB 分片下载到 `cache/*.part`；中断后根据本地安全偏移恢复。
7. 下载完成后重命名为正式 cache 文件；“下载并播放”由 `MainWindow` 切换播放页并调用 `playLocalFile()`。

## 6. 线程模型

- Qt GUI 主线程：窗口、页面、信号槽和 UI 状态。
- `TcpClient` 接收线程：阻塞式 `recvAll()` 读完整长度帧，通过 Qt 信号交回对象/UI。
- `VideoPlayer` 读取线程：解封装、分流、seek；SDL 音频回调线程解码音频；SDL 视频线程或定时器解码视频。
- `PicInPic_Read`：摄像头/桌面采集线程；`SaveVideoFileThread`：编码、交错封装线程。
- 服务端 Reactor 线程：accept/recv/send/epoll/连接状态。
- 服务端 worker：MySQL/Argon2id、媒体 SQL、上传/下载文件 I/O和协议业务响应生成。

## 7. 运行目录与持久状态

- `AVClient/cache/`：下载中的 `.part` 与完成媒体。
- `AVClient/recordings/`：默认录制文件。
- `AVClient/transfer_state/*.upload.json`：未完成上传任务。
- `AVClient/transfer_state/*.download.json`：未完成下载任务。
- `AVServer/media/`：正式远程媒体。
- `AVServer/temp/*.part`：未完成上传数据。
- `AVServer/temp/tasks/*.task`：服务端上传元数据。

## 8. 技术栈

Qt Widgets/QThread/QAudioInput/QSaveFile、C++11、FFmpeg、SDL、OpenGL、OpenCV、Winsock2、Linux socket、epoll LT、eventfd、`std::thread`、`condition_variable`、文件系统 API。

## 9. 已实现

本地播放与 seek、桌面/摄像头/麦克风录制、Ping、媒体列表、普通上传下载、上传与下载断点续传、下载后播放、多客户端 epoll Reactor、有界动态业务线程池、服务端优雅停止。阶段 1 至 10 已由用户完成人工测试。

## 10. 未实现与边界

- 无 TLS、用户登录鉴权、权限隔离、配额与数据库；上传 token 只提供任务级恢复凭证。
- 无 SHA-256 强内容校验；上传主要检查身份、文件名、大小与偏移，下载检查大小和 mtime。
- 无在线播放、自定义边下边播、多任务客户端并行、任务优先级/通用取消/自动重试。
- 协议直接传 packed struct 且使用主机字节序，当前 Windows x86-64 与 Ubuntu x86-64 可配合，但不是严格跨架构协议。
- 播放/录制模块保留较多旧 FFmpeg API 和教学代码，生产化前需升级与系统测试。
