# AVProject：基于 Qt/FFmpeg 的 C/S 音视频播放录制与远程媒体管理系统

## 项目简介

AVProject 是一个面向音视频学习与工程整合的 C++ 客户端/服务端项目。

- Windows 客户端负责本地音视频播放、桌面录制、服务器连接、远程媒体列表、分片上传、分片下载和下载后播放。
- Ubuntu 服务端负责 TCP 通信、协议分发、媒体目录扫描、上传临时文件管理和媒体文件分块读取。

项目从四个独立学习工程逐步整合而来，并保留原项目作为参考：

- `MediaPlayer`：Qt + FFmpeg + SDL + OpenGL 播放器。
- `VideoRecorder`：Qt + FFmpeg + OpenCV 录屏器。
- `NetDisk-Client`：Windows TCP 客户端封装参考。
- `NetDisk-Server`：Linux epoll、线程池和协议分发参考。

当前主线工程是 `AVClient` 和 `AVServer`。现阶段已经形成：

```text
本地播放 / 本地录制
        ↓
连接服务器并查看媒体列表
        ↓
选择本地媒体分片上传
        ↓
选择远程媒体分片下载到 cache
        ↓
调用本地播放器播放完整缓存文件
```

## 功能概览

### Windows 客户端

- Qt Widgets 统一主窗口。
- Player、Recorder、Remote Media、Settings 四个页面。
- 本地文件和网络 URL 播放。
- 播放、暂停、停止、进度显示和 seek。
- 桌面、摄像头画中画和麦克风录制。
- FLV 文件输出。
- TCP 连接、断开和 Ping/Pong。
- 远程媒体列表刷新。
- 64 KB 串行分片上传与进度显示。
- 上传断点续传，以及 AVClient/AVServer 重启后的任务恢复。
- 未完成上传任务列表、手动恢复和本地放弃记录。
- 64 KB 串行分片下载与进度显示。
- 下载到 `AVClient/cache/` 后自动切换播放页播放。

### Ubuntu 服务端

- Linux non-blocking socket + epoll LT 单线程 Reactor。
- 多客户端连接管理和每连接独立收发缓冲区。
- TCP 长度帧增量解析、非阻塞发送队列和部分写处理。
- Ping、媒体列表、上传、下载协议分发。
- 扫描 `media/` 生成远程媒体列表。
- 上传文件先写入 `temp/*.part`，任务元数据原子写入 `temp/tasks/*.task`，完成校验后移动到 `media/`。
- 服务端启动时恢复未完成上传任务，并按 `.part` 实际大小返回安全恢复偏移。
- 上传任务默认保留 72 小时，每 5 分钟低频清理一次过期且未绑定连接的任务。
- 下载时按 offset 从 `media/` 读取指定分片。
- 媒体扩展名白名单、文件名检查和路径穿越防护。
- 上传同名文件自动增加 `_1`、`_2` 后缀。
- 连接断开时只解除上传任务的当前会话绑定，保留可恢复任务和 `.part` 文件。

> 当前 `AVServer` 已完成 epoll 多客户端改造，采用单线程 Reactor。协议分发和 64 KB 文件读写仍在事件线程同步执行，尚未接入业务线程池，因此它是可演示的并发 I/O 版本，不等同于生产级高并发媒体服务器。

## 技术栈

| 范围 | 技术 |
| --- | --- |
| 客户端 UI | Qt 5.12.11 Widgets |
| Windows 编译器 | MinGW 7.3.0 32-bit |
| 播放与录制 | FFmpeg 4.2.2 |
| 音频输出 | SDL2 2.0.10 |
| 视频显示 | QOpenGLWidget / OpenGL |
| 音频采集 | Qt Multimedia / QAudioInput |
| 桌面与摄像头处理 | Qt Screen API、OpenCV 4.2.0 |
| 客户端网络 | Winsock2、C++ 接收线程、Qt signal/slot |
| 服务端网络 | Linux non-blocking socket、epoll LT、单线程 Reactor |
| 服务端构建 | Ubuntu、g++、Makefile |
| 应用层协议 | 4 字节包长 + 自定义二进制包体 |

## 系统架构

```text
AVClient (Windows / Qt)
├── PlayerPage
│   └── PlayerDialog -> VideoPlayer -> FFmpeg / SDL / OpenGL
├── RecorderPage
│   └── RecorderDialog -> SaveVideoFileThread
│       ├── PicInPic_Read
│       └── Audio_Read
├── RemoteMediaPage
│   ├── 媒体列表
│   ├── 分片上传与任务恢复
│   └── 分片下载与 cache
├── SettingsPage
└── AVNetworkClient -> TcpClient / Winsock
                         │
                         │ TCP：4 字节长度 + 包体
                         ▼
AVServer (Ubuntu)
├── AVServer：服务启动入口
├── EpollServer：监听、epoll 事件循环、非阻塞收发
├── ConnectionContext：每连接接收缓冲、发送队列和状态
├── ProtocolDispatcher：协议分发与响应生成
├── MediaManager：扫描 media/
├── UploadManager：temp/tasks 持久任务 + temp/*.part -> media/
└── DownloadManager：从 media/ 分块读取
```

详细说明见 [系统架构文档](docs/ARCHITECTURE.md)。

## 已完成阶段

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| 阶段 1 | 统一 Qt 客户端，本地播放与录制 | 完成 |
| 阶段 2 | Windows/Ubuntu TCP 通信与 Ping/Pong | 完成 |
| 阶段 3 | 远程媒体列表 | 完成 |
| 阶段 4 | 64 KB 分片上传 | 完成 |
| 阶段 5 | 64 KB 分片下载与下载后播放 | 完成 |
| 阶段 6 | 工程文档、架构说明与面试复盘 | 完成 |
| 阶段 7 | epoll LT 单线程 Reactor 与多客户端并发 | 完成 |
| 阶段 8 | 上传断点续传与客户端/服务端任务恢复 | 当前阶段 |

各阶段记录位于 [docs/stage_logs](docs/stage_logs/)。

## 目录结构

```text
AVProject/
├── AVClient/                  # 当前 Windows Qt 客户端
│   ├── modules/
│   │   ├── player/            # 播放器模块副本
│   │   ├── recorder/          # 录制器模块副本
│   │   └── network/           # TCP 与 AV 业务协议
│   ├── pages/                 # 四个业务页面
│   ├── deploy/                # DLL 部署脚本
│   ├── bin/                   # 运行目录，不提交
│   ├── cache/                 # 下载缓存，不提交
│   └── transfer_state/        # 客户端未完成上传状态，不提交
├── AVServer/                  # 当前 Ubuntu 媒体服务器
│   ├── include/               # Reactor、连接上下文、协议与业务管理器
│   ├── src/                   # epoll 网络层和协议业务实现
│   ├── media/                 # 正式媒体，不提交
│   └── temp/                  # .part 与 tasks/*.task，不提交
├── MediaPlayer/               # 原始播放器，保留
├── VideoRecorder/             # 原始录屏器，保留
├── NetDisk-Client/            # 原始客户端网络示例，保留
├── NetDisk-Server/            # 原始 Linux 服务端示例，保留
└── docs/                      # 架构、协议、构建、测试和面试文档
```

## 快速启动

### 1. Ubuntu 启动服务端

```bash
cd AVProject/AVServer
make clean
make
./AVServer 8000
```

### 2. Windows 构建客户端

```powershell
cd D:\colin\project\AVProject\AVClient\build-debug
D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe ..\AVClient.pro -spec win32-g++ "CONFIG+=debug"
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe -j4
```

运行 `AVClient/bin/AVClient.exe`，在 Settings 页连接 `192.168.44.130:8000`。

完整依赖、部署和网络检查见 [构建与运行文档](docs/BUILD_AND_RUN.md)，端到端演示顺序见 [测试流程](docs/TEST_WORKFLOW.md)。

## 当前不支持

- 通用业务线程池和异步磁盘 I/O。
- 用户注册、登录鉴权、权限和配额。
- MySQL 媒体索引。
- 下载断点续传和通用传输取消。
- 多文件并行上传或下载。
- 服务端 MD5/SHA-256 强内容完整性校验。
- 删除、重命名和搜索。
- TCP 边下边播、FFmpeg 自定义 AVIO。
- HLS、RTMP 推流服务或转码服务。

## 后续优化方向

1. 在保持 epoll 线程只管理连接状态的前提下，引入有界工作线程池处理磁盘 I/O 和耗时业务。
2. 在已有上传恢复基础上增加下载断点续传、通用取消和 SHA-256 校验。
3. 加入用户认证、权限控制和可选数据库索引。
4. 增加远程删除、重命名、搜索和分页。
5. 统一第三方依赖目录与可配置构建路径。
6. 增加自动化协议测试、传输测试和异常注入测试。
7. 完善发布包、运行截图、架构图和演示视频。

## 延伸文档

- [系统架构](docs/ARCHITECTURE.md)
- [通信协议设计](docs/PROTOCOL_DESIGN.md)
- [构建与运行](docs/BUILD_AND_RUN.md)
- [完整测试流程](docs/TEST_WORKFLOW.md)
- [面试问答复盘](docs/INTERVIEW_QA.md)
- [阶段 7：epoll 多客户端改造](docs/stage_logs/STAGE7_EPOLL_MULTI_CLIENT.md)
- [阶段 8：上传断点续传与任务恢复](docs/stage_logs/STAGE8_RESUMABLE_UPLOAD.md)
