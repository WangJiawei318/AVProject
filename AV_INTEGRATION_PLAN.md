# AVProject 项目审查与第一阶段整合方案

## 1. 项目现状分析

当前根目录包含用户指定的四个子项目：`MediaPlayer`、`VideoRecorder`、`NetDisk-Client`、`NetDisk-Server`。另外还存在 `PlayHallServer` 和若干 zip 备份，本轮暂不纳入主线。

四个项目目前都不是一个统一工程，而是独立样例项目：

- `MediaPlayer`：Qt Widgets + FFmpeg 4.2.2 + SDL2 + OpenGL，本地/网络媒体播放。
- `VideoRecorder`：Qt Widgets + Qt Multimedia + FFmpeg 4.2.2 + OpenCV 4.2.0，桌面录制、摄像头画中画、麦克风采集、FLV 输出。
- `NetDisk-Client`：Qt Widgets + Winsock TCP 客户端封装，中介者模式转发网络包。
- `NetDisk-Server`：Linux C++ 服务端，epoll + pthread 线程池 + 协议分发 + MySQL 示例。

客户端三个项目均已有 Qt 5.12.11 MinGW 7.3.0 32-bit 的 `build-debug`/`bulid-debug` 产物。建议第一阶段继续使用这套 Qt/MinGW/32-bit 环境，避免混用 MSVC 或 64-bit 第三方库。

## 2. 四个子项目功能和可复用模块清单

### MediaPlayer

入口文件：

- `MediaPlayer/main.cpp`

构建方式：

- `MediaPlayer/MediaPlayer.pro`
- `MediaPlayer/opengl/opengl.pri`
- 生成目录：`MediaPlayer/build-debug`

核心类和模块：

- `PlayerDialog`：播放器 UI，负责打开文件/URL、播放、暂停、停止、seek、显示进度和画面。
- `VideoPlayer`：播放器核心，继承 `QThread`，负责 FFmpeg 解封装/解码、音视频同步、seek、状态管理。
- `PacketQueue`：基于 SDL mutex/cond 的 AVPacket 队列，供音频和视频解码线程使用。
- `MyOpenGLWidget`：基于 `QOpenGLWidget` 的视频帧渲染控件。

可复用建议：

- 第一阶段直接复用 `PlayerDialog + VideoPlayer + PacketQueue + opengl`。
- 暂时不要拆 `VideoPlayer` 的线程和同步逻辑。
- 可把 `PlayerDialog` 先作为 `AVClient` 的播放页组件嵌入，跑通后再抽象成 `PlayerPage`。

依赖和风险：

- 使用 FFmpeg 4.2.2，包含 `.lib` 和 `.dll.a`，当前 qmake 走 MinGW。
- SDL 链接路径使用 `SDL2-2.0.10/lib/x86/SDL2.lib`，必须保持 32-bit。
- 已定义 `SDL_MAIN_HANDLED` 并在 `main.cpp` 调用 `SDL_SetMainReady()`，整合到统一 `main.cpp` 后仍要保留。
- `av_register_all()` 在 FFmpeg 4.2.2 可用，升级 FFmpeg 5/6 后需要移除。

### VideoRecorder

入口文件：

- `VideoRecorder/main.cpp`

构建方式：

- `VideoRecorder/VideoRecorder.pro`
- 生成目录名为 `bulid-debug`，存在拼写问题但不影响已有构建。

核心类和模块：

- `RecorderDialog`：录制 UI，设置保存路径，启动/停止录制，显示预览。
- `SaveVideoFileThread`：录制核心线程，管理 FFmpeg 输出上下文、H.264/AAC 编码、FLV 写文件。
- `PicInPic_Read`：采集桌面和摄像头画面，合成画中画，并转换为 YUV420P。
- `Audio_Read`：使用 `QAudioInput` 采集麦克风音频，再交给编码线程。
- `PictureWidget`：录制时的摄像头画中画显示窗口。

可复用建议：

- 第一阶段直接复用 `RecorderDialog + SaveVideoFileThread + PicInPic_Read + Audio_Read + PictureWidget`。
- 暂时不要调整编码流程、时间戳同步、缓存队列。
- 后续可以给 `SaveVideoFileThread` 补充 `finished(filePath)` 信号，用于阶段 4 上传。

依赖和风险：

- `VideoRecorder.pro` 中 OpenCV 路径写死为旧路径：`D:\colin\project\VideoPlayer\0602\VideoRecorder\opencv-release`。
- 当前项目内实际存在 `VideoRecorder/opencv-release`，第一阶段应改为 `$$PWD/opencv-release/...`。
- `common.h` 里有 Windows 风格 include：`opencv2\imgproc\types_c.h`，MinGW/Windows 可用，跨平台时需改为 `/`。
- 录制依赖桌面权限、摄像头、麦克风设备；阶段验收要允许“无摄像头时给出提示，但程序不崩溃”。

### NetDisk-Client

入口文件：

- `NetDisk-Client/main.cpp`

构建方式：

- `NetDisk-Client/NetDisk.pro`
- `NetDisk-Client/netapi/netapi.pri`
- 生成目录：`NetDisk-Client/build-debug`

核心类和模块：

- `CKernel`：网盘示例业务核，读取配置、创建网络对象、接收网络包。
- `INet`：网络接口抽象。
- `TcpClient`：Winsock TCP 客户端，实现连接、发送、接收线程、长度头拆包。
- `INetMediator`：Qt 信号中介者基类。
- `TcpClientMediator`：TCP 客户端中介者，把网络线程数据转成 Qt 信号。
- `packdef.h`：协议号和登录/注册包定义。

可复用建议：

- 第二阶段复用 `netapi/net/TcpClient.*`、`netapi/net/INet.*`、`netapi/mediator/INetMediator.*`、`TcpClientMediator.*`。
- `packdef.h` 可参考协议结构，但应改成 AV 业务协议，例如登录、媒体列表、上传、下载。
- 不建议直接复用整个 `CKernel`，因为其中硬编码 IP、启动即发登录包、ini key 有 `prot/port` 拼写不一致问题。

依赖和风险：

- Windows 专用，依赖 Winsock、`process.h`、`_beginthreadex`、`HANDLE`。
- `netapi.pri` 同时链接 `libwsock32`、`libws2_32`、`libMswsock`，第一阶段不接入网络时不需要引入。
- 发送协议为 4 字节长度头 + 业务 struct，和服务端当前协议一致，适合后续复用。

### NetDisk-Server

入口文件：

- `NetDisk-Server/src/main.cpp`

构建方式：

- `NetDisk-Server/NetDisk.pro`：qmake console app，不依赖 Qt。
- `NetDisk-Server/src/makefile`：Linux g++ makefile。

核心类和模块：

- `TcpKernel`：服务端总控，初始化 MySQL、网络层、协议映射，进入事件循环。
- `Block_Epoll_Net`：Linux TCP server，epoll accept/recv，长度头拆包，线程池投递。
- `thread_pool`：pthread 线程池。
- `CLogic`：业务协议分发处理，当前只有注册/登录示例。
- `CMysql`：MySQL 封装，提供连接、查询、更新。
- `packdef.h`：服务端协议定义、数据库配置、端口等常量。

可复用建议：

- 第三阶段保留 `Block_Epoll_Net + Thread_pool + TcpKernel + CLogic` 的框架。
- `CLogic` 改造成 AV 业务：登录、媒体列表、文件上传、文件下载。
- `CMysql` 可作为可选模块；最小可运行版可以先用本地目录扫描生成媒体列表，降低环境依赖。

依赖和风险：

- Linux-only，依赖 `sys/epoll.h`、`pthread`、`mysqlclient`。
- `packdef.h` 中数据库密码硬编码，后续必须改到配置文件或环境变量。
- 当前 `TcpKernel::Open()` 强制连接 MySQL，MySQL 不通会直接启动失败。最小 AVServer 建议先解除强依赖。
- 文件上传/下载不能一次性把大视频塞进固定 struct，应设计分片协议。

## 3. 推荐整合后的目录结构

推荐保留四个原始子项目不动，新建整合工程和公共文档：

```text
AVProject/
  MediaPlayer/              # 原始播放器项目，保留
  VideoRecorder/            # 原始录屏项目，保留
  NetDisk-Client/           # 原始客户端网络示例，保留
  NetDisk-Server/           # 原始服务端示例，保留
  AVClient/                 # 阶段 1 新建：统一 Windows Qt 客户端
    AVClient.pro
    main.cpp
    MainWindow.h/.cpp/.ui
    pages/
      PlayerPage.h/.cpp/.ui
      RecorderPage.h/.cpp/.ui
      SettingsPage.h/.cpp/.ui
    modules/
      player/               # 第一阶段复制或引用 MediaPlayer 核心文件
      recorder/             # 第一阶段复制或引用 VideoRecorder 核心文件
      network/              # 第二阶段再引入 NetDisk-Client netapi
    third_party/
      ffmpeg-4.2.2/         # 可先复用原目录，稳定后再统一
      SDL2-2.0.10/
      opencv-release/
    deploy/
      dll/
    cache/                  # 阶段 4 下载远程视频后本地播放
  AVServer/                 # 阶段 3 新建：Ubuntu 媒体服务器
    include/
    src/
    media/
    config/
    build/
  docs/
    AV_INTEGRATION_PLAN.md
    BUILD_NOTES.md
```

第一阶段为了少动原项目，可以先采用“复制核心源码到 `AVClient/modules`”的方式。这样改 `AVClient` 不会影响四个原始项目；等稳定后再考虑用 `.pri` 引用共享模块。

## 4. 第一阶段具体实施方案

目标：创建统一 Qt 客户端 `AVClient`，同一个窗口内能进入播放页面、录制页面、设置占位页面。第一阶段不接服务器，不实现远程播放，不引入 NetDisk 网络代码。

建议步骤：

1. 创建 `AVClient/AVClient.pro`，沿用 Qt 5.12.11 MinGW 32-bit，启用 `core gui widgets multimedia opengl`。
2. 创建 `AVClient/main.cpp`，统一初始化 `QApplication`、`SDL_SetMainReady()`、`av_register_all()`、`avformat_network_init()`。
3. 创建 `MainWindow`，使用 `QTabWidget` 或左侧导航 + `QStackedWidget`，包含播放、录制、设置三个页面。
4. 播放页先嵌入 `PlayerDialog` 或封装一个 `PlayerPage` 承载原播放器 UI，复用 `VideoPlayer/PacketQueue/MyOpenGLWidget`。
5. 录制页先嵌入 `RecorderDialog` 或封装一个 `RecorderPage` 承载原录制 UI，复用 `SaveVideoFileThread/PicInPic_Read/Audio_Read/PictureWidget`。
6. 先使用相对路径修正第三方库 include/lib，尤其是 VideoRecorder 的 OpenCV 路径。
7. 将 FFmpeg/SDL/OpenCV DLL 放到 `AVClient/build-debug/debug` 或统一 `AVClient/deploy/dll` 后由部署脚本复制。
8. 验证启动、打开本地视频、录制保存 FLV、停止后资源释放。

不建议第一阶段做的事：

- 不拆 `VideoPlayer` 的音视频同步逻辑。
- 不改 `SaveVideoFileThread` 的编码和队列逻辑。
- 不接 NetDisk 服务器。
- 不做 TCP 边下边播。
- 不把四个原始项目移动或删除。

## 5. 第一阶段需要新增/修改的文件清单

新增：

- `AVClient/AVClient.pro`
- `AVClient/main.cpp`
- `AVClient/MainWindow.h`
- `AVClient/MainWindow.cpp`
- `AVClient/MainWindow.ui`
- `AVClient/pages/SettingsPage.h`
- `AVClient/pages/SettingsPage.cpp`
- `AVClient/pages/SettingsPage.ui`
- `AVClient/modules/player/*`
- `AVClient/modules/recorder/*`
- `AVClient/deploy/copy_runtime_dlls.ps1` 或同等说明脚本

建议复制到 `AVClient/modules/player`：

- `MediaPlayer/playerdialog.*`
- `MediaPlayer/playerdialog.ui`
- `MediaPlayer/videoplayer.*`
- `MediaPlayer/packetqueue.*`
- `MediaPlayer/opengl/myopenglwidget.*`

建议复制到 `AVClient/modules/recorder`：

- `VideoRecorder/recorderdialog.*`
- `VideoRecorder/recorderdialog.ui`
- `VideoRecorder/savevideofilethread.*`
- `VideoRecorder/audio_read.*`
- `VideoRecorder/picinpic_read.*`
- `VideoRecorder/picturewidget.*`
- `VideoRecorder/picturewidget.ui`
- `VideoRecorder/common.h`

少量修改：

- `AVClient.pro` 中用相对路径配置 FFmpeg、SDL、OpenCV。
- 如果复制源码，修正 include 路径，例如 `#include "ui_playerdialog.h"` 仍由 uic 生成，不应手写路径。
- 如出现类名或资源冲突，只在 `AVClient` 副本内调整，不动原项目。

暂不修改：

- `MediaPlayer/`
- `VideoRecorder/`
- `NetDisk-Client/`
- `NetDisk-Server/`

## 6. 第一阶段验收标准

最小验收：

- `AVClient` 能在 Windows + Qt 5.12.11 MinGW 32-bit 下编译通过。
- 启动后出现统一主窗口。
- 播放页面能选择本地 mp4/flv/mkv 文件播放。
- 播放页面支持暂停、继续、停止、seek。
- 录制页面能设置输出路径、开始录制、停止录制。
- 录制输出文件能被本地播放器或 `AVClient` 播放页打开。
- 设置页面可以只是占位，但程序切换页面不崩溃。
- 关闭主窗口时播放线程、录制线程、SDL 音频设备能正常释放。

扩展验收：

- 无摄像头/无麦克风时程序有提示，不崩溃。
- DLL 缺失时有明确构建记录。
- README 或 `BUILD_NOTES.md` 能说明依赖版本、Qt Kit、DLL 放置位置。

## 7. 后续阶段路线

阶段 0：独立编译验证

- 分别编译运行 `MediaPlayer`、`VideoRecorder`、`NetDisk-Client`、`NetDisk-Server`。
- 记录 Qt Kit、FFmpeg/SDL/OpenCV DLL、Ubuntu g++/make/mysqlclient 依赖。
- 修正明显路径问题，但每个修复单独提交。

阶段 1：统一 Windows 客户端单机功能

- 新建 `AVClient`。
- 复用播放器和录制器核心。
- 不接服务器，只完成同一客户端内播放和录制。

阶段 2：接入 TCP 客户端

- 从 `NetDisk-Client/netapi` 引入 TCP 客户端封装。
- 新增 `AVNetworkClient` 适配层，负责连接、断开、发送协议包、接收协议包。
- 完成连接 Ubuntu 服务端、登录或 ping/pong 测试。

阶段 3：改造服务端为 `AVServer`

- 新建 `AVServer`，复用 epoll + 线程池 + 协议映射。
- 定义 AV 协议：登录、媒体列表、上传开始/分片/结束、下载请求/分片/结束。
- 第一版可以不用 MySQL，直接扫描 `media/` 目录生成列表。

阶段 4：上传、列表、下载后播放

- 录制完成后上传到服务器。
- 客户端拉取远程媒体列表。
- 下载远程文件到 `AVClient/cache/`。
- 调用本地播放器播放 cache 文件。

## 8. 风险点与注意事项

- Qt/MinGW 位数必须统一。当前依赖明显偏 32-bit，优先继续使用 Qt 5.12.11 MinGW 7.3.0 32-bit。
- `VideoRecorder.pro` OpenCV 绝对路径必须改为相对路径，否则换机器/换目录会失败。
- 播放器和录制器都用 FFmpeg 4.2.2，第一阶段不要升级 FFmpeg。
- `SDL_MAIN_HANDLED` 和 `SDL_SetMainReady()` 整合后仍需保留。
- `RecorderDialog` 启动录制会最小化主窗口并显示 `PictureWidget`，嵌入 `AVClient` 后交互可能需要微调。
- 录屏使用 `QScreen::grabWindow()`，在高 DPI、多屏、权限受限环境可能有问题。
- `NetDisk-Client` 是 Windows Winsock 封装，不能直接给 Ubuntu 服务端使用。
- `NetDisk-Server` 当前强依赖 MySQL，最小媒体服务器建议先解除强依赖。
- 文件传输不能用固定 4096 buffer 一次性发送大文件，应在阶段 3 设计分片。
- 当前根目录不是 Git 仓库；若要按 commit 推进，建议先 `git init` 或确认真正仓库位置。

## 9. 建议的 git commit 计划

阶段 0：

- `chore: add project audit docs`
- `build: document baseline build environment`
- `fix(videorecorder): use relative OpenCV paths`
- `build: verify original MediaPlayer standalone build`
- `build: verify original VideoRecorder standalone build`
- `build: verify original NetDisk client/server builds`

阶段 1：

- `feat(avclient): add Qt shell with player recorder settings tabs`
- `feat(avclient): integrate MediaPlayer module`
- `feat(avclient): integrate VideoRecorder module`
- `build(avclient): add third-party library paths and runtime dll notes`
- `test(avclient): verify local playback and recording workflow`

阶段 2：

- `feat(avclient): add TCP client adapter`
- `feat(protocol): add AV login and ping packets`
- `test(network): verify client-server basic packet exchange`

阶段 3：

- `feat(avserver): add epoll media server skeleton`
- `feat(avserver): implement login and media list protocols`
- `feat(avserver): implement upload and download chunk protocols`

阶段 4：

- `feat(avclient): upload recorded files`
- `feat(avclient): show remote media list`
- `feat(avclient): download media to cache and play locally`
