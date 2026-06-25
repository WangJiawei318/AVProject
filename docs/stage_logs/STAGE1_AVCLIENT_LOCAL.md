# 阶段 1 开发记录：AVClient 本地播放与本地录制整合

## 1. 本阶段目标

阶段 1 的目标是先创建一个统一的 Windows Qt 客户端 `AVClient`，把原有 `MediaPlayer` 播放器和 `VideoRecorder` 录制器放到同一个 Qt Widgets 程序中运行。这个阶段的核心验证点不是网络能力，而是验证两个已有音视频模块能否在同一个客户端进程中稳定共存。

之所以先做统一 Windows 客户端，是因为后续 C/S 项目中的客户端最终要同时承担本地播放、录制、网络连接、上传下载和远程媒体管理等职责。如果一开始就直接做网络和服务器，问题会混在一起，很难判断错误来自播放、录制、网络协议还是服务端。阶段 1 先把本地能力跑通，相当于先打好客户端底座。

阶段 1 只整合本地播放和本地录制，暂时不接入 `NetDisk-Client` 网络模块，主要原因有三个：

- 播放器和录制器都涉及 FFmpeg、线程、设备、DLL、Qt UI 事件循环，先单独验证它们在同一 Qt 程序中是否冲突。
- `NetDisk-Client` 的 `CKernel` 仍带有网盘示例业务、硬编码 IP 和登录包逻辑，直接接入会扩大修改范围。
- 网络上传、下载、媒体列表、远程播放需要客户端和服务端协议同时变化，不适合作为第一步。

本阶段也不直接做上传、下载和远程播放。远程播放第一版计划采用“先下载到本地 cache，再调用播放器播放”的方案，这依赖阶段 2、阶段 3 先完成基础 C/S 通信和文件传输协议。阶段 1 在整个 `AVProject` 中的位置是客户端单机整合阶段，为后续网络接入提供稳定 UI 和本地音视频能力。

明确目标：阶段 1 的核心目标是先验证 `MediaPlayer` 和 `VideoRecorder` 能否在同一个 Qt 客户端中稳定共存。

## 2. 本阶段完成的功能

- 新建 `AVClient` 工程，使用 qmake `.pro` 构建。
- 创建统一 Qt Widgets 主窗口 `MainWindow`。
- 主窗口使用 `QTabWidget` 管理播放、录制、设置三个页面。
- 创建播放页 `PlayerPage`。
- 创建录制页 `RecorderPage`。
- 创建设置页 `SettingsPage`，当前作为阶段 1 占位。
- 复制并整合原 `MediaPlayer` 播放器模块到 `AVClient/modules/player/`。
- 复制并整合原 `VideoRecorder` 录制器模块到 `AVClient/modules/recorder/`。
- 播放页可以打开本地视频文件。
- 播放页保留原播放器的播放、暂停、停止、seek 等能力。
- 录制页可以开始和停止本地录制。
- 默认录制输出目录改为 `AVClient/bin/recordings/`。
- 录制文件命名格式为 `record_yyyyMMdd_hhmmss.flv`。
- 录制完成的 `.flv` 文件可以被播放页打开播放。
- 新增 `AVClient/deploy/copy_runtime_dlls.ps1`，用于复制运行时 DLL。
- 原始四个子项目 `MediaPlayer`、`VideoRecorder`、`NetDisk-Client`、`NetDisk-Server` 没有删除、移动或直接修改源码。
- 阶段 1 不包含服务器连接、上传、下载、媒体列表和远程播放。

## 3. 新增文件清单

### `AVClient/`

`AVClient/` 是阶段 1 新建的统一 Windows 客户端工程目录。它不替代原始 `MediaPlayer` 和 `VideoRecorder`，而是保存整合后的客户端壳和复制过来的模块副本。

### `AVClient/AVClient.pro`

作用：`AVClient` 的 qmake 工程文件。

归属：构建配置。

与原项目关系：参考了 `MediaPlayer/MediaPlayer.pro` 和 `VideoRecorder/VideoRecorder.pro`，但改成统一工程配置，集中声明 Qt 模块、源码、头文件、UI 文件、FFmpeg、SDL、OpenCV 路径。

### `AVClient/main.cpp`

作用：`AVClient` 程序入口。

归属：应用入口和全局初始化。

与原项目关系：参考 `MediaPlayer/main.cpp` 中 FFmpeg 和 SDL 初始化方式，同时承担统一客户端的 `QApplication` 创建和 `MainWindow` 启动。

### `AVClient/MainWindow.h` 和 `AVClient/MainWindow.cpp`

作用：统一主窗口。

归属：UI 层。

与原项目关系：这是阶段 1 新增的整合层，原始 `MediaPlayer` 和 `VideoRecorder` 都没有这个统一主窗口。

### `AVClient/pages/PlayerPage.h` 和 `AVClient/pages/PlayerPage.cpp`

作用：播放页面包装类，负责把播放器 UI 嵌入 `AVClient` 主窗口。

归属：UI 层，播放页容器。

与原项目关系：复用 `MediaPlayer` 中复制过来的 `PlayerDialog`，不直接改 `VideoPlayer` 的播放核心。

### `AVClient/pages/RecorderPage.h` 和 `AVClient/pages/RecorderPage.cpp`

作用：录制页面包装类，负责把录制器 UI 嵌入 `AVClient` 主窗口。

归属：UI 层，录制页容器。

与原项目关系：复用 `VideoRecorder` 中复制过来的 `RecorderDialog`，只调用其嵌入模式接口。

### `AVClient/pages/SettingsPage.h` 和 `AVClient/pages/SettingsPage.cpp`

作用：设置页占位。

归属：UI 层。

与原项目关系：阶段 1 新增，后续阶段 2 可扩展为服务器 IP、端口、连接、断开、Ping 测试等网络配置入口。

### `AVClient/modules/player/`

作用：保存从 `MediaPlayer` 复制来的播放器模块副本。

归属：播放模块。

包含文件：

- `playerdialog.h`
- `playerdialog.cpp`
- `playerdialog.ui`
- `videoplayer.h`
- `videoplayer.cpp`
- `PacketQueue.h`
- `PacketQueue.cpp`
- `myopenglwidget.h`
- `myopenglwidget.cpp`

与原项目关系：这些文件来自 `MediaPlayer`。阶段 1 采用复制副本的方式，是为了让 `AVClient` 可以独立适配和演进，同时不破坏原播放器项目。

### `AVClient/modules/recorder/`

作用：保存从 `VideoRecorder` 复制来的录制器模块副本。

归属：录制模块。

包含文件：

- `recorderdialog.h`
- `recorderdialog.cpp`
- `recorderdialog.ui`
- `savevideofilethread.h`
- `savevideofilethread.cpp`
- `picinpic_read.h`
- `picinpic_read.cpp`
- `audio_read.h`
- `audio_read.cpp`
- `picturewidget.h`
- `picturewidget.cpp`
- `picturewidget.ui`
- `common.h`

与原项目关系：这些文件来自 `VideoRecorder`。阶段 1 只在 `AVClient` 副本中做少量嵌入适配，不修改原始 `VideoRecorder` 项目。

### `AVClient/deploy/copy_runtime_dlls.ps1`

作用：复制运行时 DLL 到 `AVClient/bin`。

归属：部署脚本。

与原项目关系：DLL 来源是 `MediaPlayer/dll/*.dll` 和 `VideoRecorder/dll/*.dll`，包括 FFmpeg、SDL、OpenCV 等运行时库。

### `AVClient/cache/`

作用：阶段 4 远程媒体下载缓存目录预留。

归属：运行时目录。

阶段 1 暂不使用该目录。

### `AVClient/build-debug/` 和 `AVClient/bin/`

作用：构建输出目录和运行目录。

归属：构建产物和运行产物。

说明：这些目录属于生成内容，不建议提交。当前 `.gitignore` 已包含 `AVClient/build*/`、`*.exe`、`*.dll`、媒体文件等忽略规则。

### `AVClient/AVClient.pro.user`

作用：Qt Creator 用户配置文件。

归属：IDE 本地配置。

说明：这是 Qt Creator 可能自动生成的文件，不属于阶段 1 需要提交的源码，通常由 `.gitignore` 中的 `*.pro.user` 忽略。

## 4. 修改文件清单

阶段 1 的功能代码修改范围控制在 `AVClient` 内部。原始四个子项目没有直接修改。

### `AVClient/AVClient.pro`

该文件完成统一工程配置：

- `QT += core gui widgets multimedia opengl`：启用 Qt 核心、GUI、Widgets、多媒体、OpenGL 模块。
- `CONFIG += c++11`：保持和原项目一致的 C++11 标准。
- `DEFINES += SDL_MAIN_HANDLED`：避免 SDL 接管 `main` 函数。
- `DESTDIR = $$PWD/bin`：把 `AVClient.exe` 输出到 `AVClient/bin`。
- `SOURCES`、`HEADERS`、`FORMS`：统一纳入主窗口、页面包装、播放器模块、录制器模块和 UI 文件。
- `INCLUDEPATH`：使用相对路径引用 `MediaPlayer/ffmpeg-4.2.2/include`、`MediaPlayer/SDL2-2.0.10/include`、`VideoRecorder/opencv-release/include`。
- `LIBS`：链接 FFmpeg、SDL2、OpenCV 和 OpenGL。

这里没有把第三方库复制进 `AVClient`，而是通过相对路径引用原项目中的库目录。这样减少磁盘复制和版本分裂，同时仍然保持原始源码不动。

### `AVClient/main.cpp`

该文件负责程序启动和全局初始化：

- 调用 `SDL_SetMainReady()`，配合 `.pro` 中的 `SDL_MAIN_HANDLED`。
- 创建 `QApplication`。
- 设置应用名 `AVClient` 和组织名 `AVProject`。
- 调用 `av_register_all()`，适配当前 FFmpeg 4.2.2。
- 调用 `avformat_network_init()`，保留 FFmpeg 网络协议初始化能力，虽然阶段 1 不主动做 C/S。
- 打印 FFmpeg 版本，便于确认链接到正确库。
- 创建并显示 `MainWindow`。

### `AVClient/MainWindow.cpp`

`MainWindow` 是统一主窗口。当前使用 `QTabWidget` 管理三个页面：

- `PlayerPage`：播放页。
- `RecorderPage`：录制页。
- `SettingsPage`：设置页占位。

使用 `QTabWidget` 的原因是阶段 1 需要快速稳定地把两个已有 UI 放在同一个窗口内。它比自定义导航更简单，切换逻辑清晰，也减少额外 UI 风险。

### `AVClient/pages/PlayerPage.cpp`

`PlayerPage` 的核心工作是创建 `PlayerDialog` 并把它作为普通 Qt Widget 嵌入页面：

- `m_playerDialog = new PlayerDialog(this)`。
- `setWindowFlags(Qt::Widget)`：让原来的 `QDialog` 以子控件方式嵌入，而不是作为独立弹窗。
- `QVBoxLayout` 承载播放器界面。
- 设置 `QSizePolicy::Expanding`，让播放器随页面尺寸扩展。

播放逻辑仍在复制来的 `PlayerDialog`、`VideoPlayer`、`PacketQueue`、`MyOpenGLWidget` 中。

### `AVClient/pages/RecorderPage.cpp`

`RecorderPage` 的核心工作是创建 `RecorderDialog` 并嵌入页面：

- `m_recorderDialog = new RecorderDialog(this)`。
- 调用 `m_recorderDialog->setEmbeddedMode(true)`。
- `setWindowFlags(Qt::Widget)`：让原录制对话框作为页面控件显示。
- 使用 `QVBoxLayout` 承载录制界面。

录制逻辑仍在复制来的 `RecorderDialog`、`SaveVideoFileThread`、`PicInPic_Read`、`Audio_Read`、`PictureWidget` 中。

### `AVClient/pages/SettingsPage.cpp`

`SettingsPage` 当前只是占位页面：

- 使用 `QFormLayout` 放置“服务器”和“缓存目录”两个禁用输入框。
- 使用提示文本说明阶段 1 只整合本地播放和本地录制。

后续阶段 2 可以在这里增加服务器 IP、端口、连接、断开、发送 Ping 等控件。

### `AVClient/modules/recorder/recorderdialog.h` 和 `recorderdialog.cpp`

这是录制器副本中做过少量适配的文件。适配点如下：

- 新增 `setEmbeddedMode(bool embedded)`，用于告诉 `RecorderDialog` 当前运行在 `AVClient` 页面内。
- 新增 `m_embeddedMode`，控制点击开始录制时是否最小化窗口。
- 新增 `m_isRecording`，避免未开始录制时误点停止导致底层关闭未初始化资源。
- 构造函数中使用 `QCoreApplication::applicationDirPath()` 获取 `AVClient/bin`。
- 构造函数中创建 `recordings` 目录。
- 默认输出路径设置为 `AVClient/bin/recordings/record_yyyyMMdd_hhmmss.flv`。
- 点击开始录制时，如果是嵌入模式，不再执行 `showMinimized()`。
- 点击停止录制时，先判断 `m_isRecording`，再关闭采集和编码线程。
- 析构时如果仍在录制，会尝试关闭录制并等待线程结束。

这些修改只存在于 `AVClient/modules/recorder` 副本，不影响原始 `VideoRecorder`。

### 为什么不修改原始四个子项目

原始四个子项目是学习和参考基线：

- `MediaPlayer` 保留独立播放器能力。
- `VideoRecorder` 保留独立录屏器能力。
- `NetDisk-Client` 保留网络封装示例。
- `NetDisk-Server` 保留 Linux 服务端框架示例。

如果直接修改或移动原始项目，一旦整合失败，就很难回退和对照。因此阶段 1 采用“保留原始项目 + 在 `AVClient` 内复制模块副本”的方式。这样既能稳定推进整合，又能保留原项目作为可运行参考。

## 5. 当前目录结构说明

阶段 1 后核心目录结构如下：

```text
AVProject/
  MediaPlayer/
  VideoRecorder/
  NetDisk-Client/
  NetDisk-Server/
  AVClient/
    AVClient.pro
    main.cpp
    MainWindow.h
    MainWindow.cpp
    pages/
      PlayerPage.h
      PlayerPage.cpp
      RecorderPage.h
      RecorderPage.cpp
      SettingsPage.h
      SettingsPage.cpp
    modules/
      player/
        playerdialog.h/.cpp/.ui
        videoplayer.h/.cpp
        PacketQueue.h/.cpp
        myopenglwidget.h/.cpp
      recorder/
        recorderdialog.h/.cpp/.ui
        savevideofilethread.h/.cpp
        picinpic_read.h/.cpp
        audio_read.h/.cpp
        picturewidget.h/.cpp/.ui
        common.h
    deploy/
      copy_runtime_dlls.ps1
    bin/
      AVClient.exe
      recordings/
    build-debug/
    cache/
  docs/
    stage_logs/
      STAGE1_AVCLIENT_LOCAL.md
```

采用“保留原始项目 + 在 `AVClient` 内复制模块副本”的方式，是为了降低第一阶段风险。原始项目仍然能独立编译、独立运行；`AVClient` 可以在副本上做必要的路径、嵌入、默认输出目录等适配。等后续项目结构稳定后，再考虑把播放器和录制器抽象成公共库或 `.pri` 共享模块。

## 6. 核心类和模块说明

### `MainWindow`

`MainWindow` 负责统一主窗口。它继承 `QMainWindow`，在构造函数中创建 `QTabWidget`，并把三个页面加入 tab：

- `PlayerPage`
- `RecorderPage`
- `SettingsPage`

当前使用 `QTabWidget`，而不是复杂的自定义导航，是为了阶段 1 先保证能编译、能启动、能切换页面。统一主窗口的意义是把原来两个独立 Qt 程序整合到一个客户端入口中，为后续网络设置、媒体列表、上传下载等功能提供统一承载位置。

### `PlayerPage`

`PlayerPage` 是播放页容器。它不重新实现播放器，而是复用从 `MediaPlayer` 复制来的 `PlayerDialog`。

复用关系：

- `PlayerDialog`：播放器 UI，负责打开文件、按钮控制、进度显示、画面显示。
- `VideoPlayer`：播放核心线程，负责 FFmpeg 解封装、解码、音视频同步、seek。
- `PacketQueue`：音视频 packet 队列，用于解耦读取线程和解码线程。
- `MyOpenGLWidget`：视频帧渲染控件。

播放器内部大致流程：

1. 用户在 `PlayerDialog` 点击打开本地文件。
2. `PlayerDialog` 把文件路径传给 `VideoPlayer`。
3. `VideoPlayer` 使用 FFmpeg 打开媒体文件。
4. FFmpeg 解析容器格式，找到音频流和视频流。
5. 读取线程通过 `av_read_frame()` 持续读取 packet。
6. 音频 packet 放入音频 `PacketQueue`，视频 packet 放入视频 `PacketQueue`。
7. 音频由 SDL 音频回调取 packet 解码并播放。
8. 视频由解码线程取 packet 解码成图像帧。
9. 视频帧通过 Qt 信号传回 UI。
10. `MyOpenGLWidget` 接收 `QImage` 并使用 OpenGL 渲染。
11. 播放同步以音频时钟为主，视频根据时间戳等待或追赶。
12. seek 时清空旧 packet 队列，重新定位到目标时间附近。

阶段 1 没有重写这些核心流程，只是把播放器 UI 嵌入到了 `AVClient` 页面中。

### `RecorderPage`

`RecorderPage` 是录制页容器。它不重新实现录屏逻辑，而是复用从 `VideoRecorder` 复制来的 `RecorderDialog`。

复用关系：

- `RecorderDialog`：录制 UI，负责开始、停止、输出路径和预览。
- `SaveVideoFileThread`：录制核心线程，负责 FFmpeg 编码、封装、写文件。
- `PicInPic_Read`：负责桌面图像采集、摄像头采集、画中画合成和图像格式转换。
- `Audio_Read`：负责麦克风音频采集。
- `PictureWidget`：显示摄像头画中画预览。

录制器内部大致流程：

1. 用户进入 `RecorderPage`。
2. `RecorderPage` 创建并嵌入 `RecorderDialog`。
3. 用户点击开始录制。
4. `RecorderDialog` 构造 `STRU_AV_FORMAT`，设置文件名、帧率、分辨率、码率、是否采集摄像头、桌面和音频。
5. `SaveVideoFileThread::slot_setInfo()` 初始化 FFmpeg 输出上下文。
6. 视频编码器使用 H.264，音频编码器使用 AAC。
7. `PicInPic_Read` 采集桌面和摄像头图像。
8. 图像经过 RGB 到 YUV420P 转换后进入视频队列。
9. `Audio_Read` 使用 `QAudioInput` 采集 PCM 音频。
10. 音频数据进入音频队列。
11. `SaveVideoFileThread::run()` 从音视频队列取数据。
12. 视频编码为 H.264，音频编码为 AAC。
13. FFmpeg 将音视频封装为 FLV。
14. 文件保存到 `AVClient/bin/recordings/`。
15. 用户点击停止后，停止采集，写入 trailer，关闭编码器和输出文件。

阶段 1 只对 `RecorderDialog` 副本做了嵌入适配，没有重写采集、编码和封装核心逻辑。

### `SettingsPage`

`SettingsPage` 当前只是阶段 1 占位。它显示禁用的服务器和缓存目录输入框，并提示当前只整合本地播放和本地录制。

后续阶段 2 可在这里加入：

- 服务器 IP。
- 服务器端口。
- 连接按钮。
- 断开按钮。
- 发送 Ping 按钮。
- 连接状态显示。

### `copy_runtime_dlls.ps1`

`copy_runtime_dlls.ps1` 是运行时 DLL 拷贝脚本。

需要该脚本的原因是 Windows 下 Qt、FFmpeg、SDL、OpenCV 依赖动态库。程序启动时，系统会在 exe 所在目录、系统目录和 PATH 中查找 DLL。如果 `AVClient.exe` 附近没有所需 DLL，就会出现启动失败或运行时报错。

脚本当前复制：

- `MediaPlayer/dll/*.dll`
- `VideoRecorder/dll/*.dll`

这些 DLL 包含：

- FFmpeg：`avcodec-58.dll`、`avformat-58.dll`、`avutil-56.dll`、`swscale-5.dll`、`swresample-3.dll` 等。
- SDL：`SDL2.dll`。
- OpenCV：`libopencv_core420.dll`、`libopencv_imgproc420.dll`、`libopencv_videoio420.dll` 等。

Qt 自身 DLL 通常由 Qt Creator 运行环境或 Qt bin PATH 提供。若脱离 Qt Creator 单独运行，也可能需要使用 `windeployqt` 或把 Qt DLL 放到 `AVClient/bin`。

## 7. 阶段 1 整体运行流程

启动流程：

```text
启动 AVClient
        |
        v
main.cpp 初始化 SDL / Qt / FFmpeg
        |
        v
创建 QApplication
        |
        v
创建 MainWindow
        |
        v
MainWindow 创建 QTabWidget
        |
        v
加载 PlayerPage / RecorderPage / SettingsPage
```

播放流程：

```text
用户进入播放页
        |
        v
PlayerPage 中嵌入 PlayerDialog
        |
        v
用户选择本地视频文件
        |
        v
VideoPlayer 使用 FFmpeg 打开文件
        |
        v
解封装并读取音视频 packet
        |
        v
packet 分别进入音频队列和视频队列
        |
        v
SDL 播放音频，视频线程解码画面
        |
        v
OpenGL 控件渲染视频帧
```

录制流程：

```text
用户进入录制页
        |
        v
RecorderPage 中嵌入 RecorderDialog
        |
        v
点击开始录制
        |
        v
采集桌面 / 摄像头 / 麦克风
        |
        v
图像转换为 YUV，音频采集为 PCM
        |
        v
编码为 H.264 / AAC
        |
        v
封装为 FLV
        |
        v
保存到 AVClient/bin/recordings/
        |
        v
用户回到播放页打开录制文件
```

## 8. 构建方法

项目根目录：

```text
D:\colin\project\AVProject
```

推荐使用 Qt 5.12.11 MinGW 7.3.0 32-bit，与原始三个 Windows 客户端项目保持一致。

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

最终运行：

```powershell
D:\colin\project\AVProject\AVClient\bin\AVClient.exe
```

说明：

- 当前 PATH 中可能存在 Anaconda 的 `qmake.exe`，所以建议显式调用 `D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe`。
- 若 qmake 提示找不到 `g++`，需要把 `D:\Software\Qt\Tools\mingw730_32\bin` 加入 PATH。
- DLL 拷贝脚本需要在构建后执行，否则 exe 运行时可能找不到 FFmpeg、SDL、OpenCV DLL。

## 9. 测试方法

1. 启动 `D:\colin\project\AVProject\AVClient\bin\AVClient.exe`。
2. 进入播放页。
3. 点击打开本地视频文件。
4. 测试播放、暂停、继续、停止。
5. 拖动或点击进度条，测试 seek。
6. 切换到录制页。
7. 确认输出路径默认指向 `AVClient/bin/recordings/record_时间.flv`。
8. 点击开始录制。
9. 录制几秒后点击停止。
10. 检查 `AVClient/bin/recordings/` 下是否生成 `.flv` 文件。
11. 回到播放页，打开刚生成的 `.flv`。
12. 确认录制文件可以正常播放。
13. 切换到设置页，确认程序不崩溃。
14. 关闭主窗口，观察是否能正常退出。

## 10. 常见问题与排查

### `AVClient.exe` 启动失败

可能原因：

- 缺少运行时 DLL。
- Qt DLL 不在 PATH 或 exe 目录附近。
- FFmpeg、SDL、OpenCV DLL 未复制到 `AVClient/bin`。

排查方法：

- 先执行 `AVClient/deploy/copy_runtime_dlls.ps1`。
- 在 Qt Creator 中运行一次，确认 Qt 环境是否完整。
- 使用 `windeployqt AVClient.exe` 补齐 Qt 运行库。

### 缺少 FFmpeg / SDL / OpenCV / Qt DLL

可能原因：

- 只编译了 exe，没有执行 DLL 拷贝脚本。
- `MediaPlayer/dll` 或 `VideoRecorder/dll` 中缺少对应 DLL。

排查方法：

- 检查 `AVClient/bin` 是否存在 `avcodec-58.dll`、`avformat-58.dll`、`SDL2.dll`、`libopencv_core420.dll` 等。
- 重新执行 `copy_runtime_dlls.ps1`。
- 若缺 Qt DLL，运行 Qt 自带 `windeployqt`。

### qmake 找不到第三方库

可能原因：

- `AVClient.pro` 中相对路径不正确。
- 当前工作目录不是 `AVClient/build-debug`。
- 项目目录移动后第三方库路径变化。

排查方法：

- 检查 `MediaPlayer/ffmpeg-4.2.2` 是否存在。
- 检查 `MediaPlayer/SDL2-2.0.10` 是否存在。
- 检查 `VideoRecorder/opencv-release` 是否存在。
- 从 `AVClient/build-debug` 执行 `qmake ..\AVClient.pro`。

### MinGW 32-bit 和第三方库位数不匹配

可能原因：

- 使用了 64-bit Qt Kit。
- 链接了 32-bit 的 FFmpeg/SDL/OpenCV。

排查方法：

- 使用 `D:\Software\Qt\5.12.11\mingw73_32`。
- 使用 `D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe`。
- 不要混用 MSVC Kit 或 64-bit MinGW Kit。

### 播放页打不开视频

可能原因：

- 文件路径不存在或格式不支持。
- FFmpeg DLL 缺失。
- 文件被占用或权限不足。

排查方法：

- 先使用常见 mp4 文件测试。
- 检查 `AVClient/bin` 中 FFmpeg DLL。
- 查看程序控制台或调试输出。

### 视频有画面无声音

可能原因：

- SDL2 DLL 缺失。
- 系统音频设备异常。
- 视频文件音频编码不支持或音频流解析失败。

排查方法：

- 检查 `AVClient/bin/SDL2.dll`。
- 用原始 `MediaPlayer` 播放同一文件对照。
- 换一个带 AAC 音频的 mp4 测试。

### 播放 seek 异常

可能原因：

- 文件关键帧间隔过大。
- seek 后旧 packet 未及时清空。
- 网络 URL 或直播流不支持精确 seek。

排查方法：

- 用本地 mp4 测试。
- 避免用直播流测试 seek。
- 对照原 `MediaPlayer` 行为，确认是否是整合引入的问题。

### 录制页无法打开摄像头

可能原因：

- 没有摄像头设备。
- 摄像头被其他程序占用。
- OpenCV 设备编号不匹配。

排查方法：

- 关闭其他占用摄像头的软件。
- 用原始 `VideoRecorder` 对照测试。
- 暂时只测试桌面和音频录制。

### 麦克风无权限或无输入设备

可能原因：

- Windows 隐私设置禁止桌面应用访问麦克风。
- 没有默认输入设备。
- `QAudioInput` 初始化失败。

排查方法：

- 检查 Windows 麦克风隐私权限。
- 检查系统默认录音设备。
- 用原始 `VideoRecorder` 对照测试。

### 停止录制后文件损坏

可能原因：

- 录制线程未完成 trailer 写入就退出。
- 程序被强制关闭。
- 输出路径不可写。

排查方法：

- 点击停止后等待几秒再打开文件。
- 确认 `AVClient/bin/recordings` 可写。
- 不要直接结束进程。

### 录制生成文件无法播放

可能原因：

- 编码器初始化失败。
- 录制时间太短。
- 停止流程未正常完成。

排查方法：

- 录制 5 秒以上再停止。
- 用 `AVClient` 播放页和外部播放器分别测试。
- 检查文件大小是否明显为 0 或过小。

### 关闭窗口时线程未释放

可能原因：

- 播放器还在读取或解码。
- 录制线程还在写文件。
- SDL 音频回调仍在运行。

排查方法：

- 先停止播放和录制，再关闭窗口。
- 若复现问题，重点检查 `PlayerDialog` 析构中 `m_player->stop(true)` 和 `RecorderDialog` 析构中的停止等待逻辑。

### 高 DPI 或多屏情况下录制画面异常

可能原因：

- `QScreen::geometry()` 和实际缩放比例不一致。
- 只采集了主屏。
- Windows 缩放比例影响截图尺寸。

排查方法：

- 先在单屏、100% 缩放下测试。
- 再逐步测试多屏和高 DPI。
- 后续可引入屏幕选择和 DPI 适配。

## 11. 本阶段未做内容

阶段 1 没有做以下内容：

- C/S 通信。
- 服务器连接。
- Ping/Pong 协议。
- 用户登录。
- 文件上传。
- 文件下载。
- 远程媒体列表。
- 远程播放。
- MySQL。
- epoll 服务端。
- 自定义 TCP 边下边播。

这些内容会在后续阶段逐步实现。阶段 2 只建议做最基础的客户端和服务端连通性验证，不直接做上传下载。

## 12. 下一阶段建议

阶段 2 建议目标：

- 在 `AVClient` 中新增网络模块目录，例如 `AVClient/modules/network/`。
- 从 `NetDisk-Client/netapi` 中复用 TCP 客户端基础封装。
- 在设置页增加服务器 IP、端口、连接、断开、发送 Ping。
- 新建最小 `AVServer`，先不接数据库，不做媒体管理。
- 使用 `192.168.44.130:8000` 打通 Windows 客户端和 Ubuntu 服务端。
- 定义最小协议 `PING_RQ / PING_RS`。
- 验证客户端发送 Ping，服务端返回 Pong 或 Ping 响应。
- 暂时不做上传、下载、媒体列表和远程播放。

阶段 2 的重点是证明 C/S 基础通信可用，而不是扩展复杂业务。

## 13. 面试问答准备

### 1. 为什么先整合播放器和录制器，而不是直接做 C/S？

因为播放器和录制器是这个项目最核心的音视频能力。如果它们不能在同一个客户端里稳定运行，后面接入网络也没有意义。先做本地整合可以把问题范围缩小到 Qt、FFmpeg、SDL、OpenCV、线程和 DLL 环境，避免一开始就把网络协议、服务端、数据库、文件传输混在一起。

面试时可以说：我采用阶段化开发，先验证客户端本地音视频能力，再做网络通信，最后做上传下载和媒体管理。这样每个阶段都有明确验收标准。

### 2. 为什么保留原始四个子项目不动？

原始四个子项目是可运行参考和回退基线。`MediaPlayer` 和 `VideoRecorder` 已经分别验证过播放和录制能力，`NetDisk-Client` 和 `NetDisk-Server` 也保存了老师示例中的网络封装思路。如果直接修改原项目，一旦整合失败，很难判断是原功能坏了，还是整合引入了问题。

所以阶段 1 只新增 `AVClient`，并在 `AVClient` 内复制需要的模块副本。这样原项目仍然可以单独运行和对照测试。

### 3. 为什么在 `AVClient` 中复制播放器和录制器模块副本？

复制副本可以隔离整合改动。比如录制器独立运行时点击开始会最小化窗口，但嵌入 `AVClient` 后这个行为不合适，需要在副本里加嵌入模式。如果直接改原 `VideoRecorder`，可能破坏原独立录制器的行为。

阶段 1 采用复制方式是为了稳定优先。后续如果结构成熟，可以再把公共模块抽成库或 `.pri`。

### 4. `MediaPlayer` 的播放流程是什么？

用户在 `PlayerDialog` 选择本地文件后，`VideoPlayer` 使用 FFmpeg 打开文件并解析音视频流。读取线程不断调用 `av_read_frame()` 获取压缩 packet，音频 packet 放入音频队列，视频 packet 放入视频队列。

音频由 SDL 音频回调取出 packet 解码并播放，视频由视频解码线程取出 packet 解码成图像帧，再通过 Qt 信号传给 UI，最后由 `MyOpenGLWidget` 渲染到界面。同步上主要以音频时钟为参考，视频根据时间戳进行等待或追赶。

### 5. `VideoRecorder` 的录制流程是什么？

录制由 `RecorderDialog` 发起，核心工作在 `SaveVideoFileThread`。桌面和摄像头由 `PicInPic_Read` 采集，摄像头可以叠加成画中画，图像再转换成 YUV420P。麦克风由 `Audio_Read` 使用 Qt Multimedia 的 `QAudioInput` 采集 PCM 数据。

`SaveVideoFileThread` 从音视频队列取数据，视频编码成 H.264，音频编码成 AAC，再通过 FFmpeg 封装成 FLV 文件保存到本地。

### 6. FFmpeg 在播放和录制中分别承担什么作用？

播放时，FFmpeg 主要负责打开媒体文件、解析容器、查找音视频流、读取 packet、解码音频和视频。

录制时，FFmpeg 主要负责创建输出上下文、创建音视频流、打开编码器、把视频编码成 H.264、把音频编码成 AAC，并最终封装成 FLV 文件。

简单说，播放时 FFmpeg 做解封装和解码；录制时 FFmpeg 做编码和封装。

### 7. SDL 在播放器中承担什么作用？

SDL 主要用于音频播放和线程同步辅助。播放器中 SDL 音频设备通过回调函数不断请求 PCM 数据，音频解码后的数据被送给 SDL 播放。`PacketQueue` 也使用了 SDL 的 mutex 和 condition 来实现线程安全队列。

因为音频是播放同步的重要参考，所以 SDL 音频回调和音频时钟对播放器同步非常关键。

### 8. OpenGL 在播放器中承担什么作用？

OpenGL 用于视频画面渲染。`VideoPlayer` 解码出视频帧后转换成 `QImage`，通过信号发送给 UI，`MyOpenGLWidget` 接收图像并用 OpenGL 纹理绘制到窗口上。

相比直接用 QLabel 缩放图片，OpenGL 更适合持续刷新视频画面，渲染效率和显示效果更好。

### 9. OpenCV 在录制器中承担什么作用？

OpenCV 主要用于摄像头采集。`PicInPic_Read` 中使用 OpenCV 的 `VideoCapture` 打开摄像头，获取摄像头画面，再和桌面截图进行画中画合成。

桌面截图主要来自 Qt 的 `QScreen`，摄像头部分依赖 OpenCV。

### 10. 为什么录制输出选择 FLV？

当前录制器使用 FFmpeg 输出 FLV，视频编码 H.264，音频编码 AAC。FLV 对 H.264/AAC 支持成熟，封装结构相对简单，也适合后续扩展到 RTMP 推流或媒体服务器存储。

阶段 1 的目标是本地录制文件能被播放器打开，FLV 可以满足这个目标。

### 11. 为什么统一使用 Qt 5.12.11 MinGW 32-bit？

原来的 `MediaPlayer`、`VideoRecorder`、`NetDisk-Client` 都是基于 Qt 5.12.11 MinGW 7.3.0 32-bit 构建过的。项目内的 FFmpeg、SDL、OpenCV 库也与这个环境匹配。

如果换成 MSVC 或 64-bit Qt，很容易出现库位数不匹配、链接失败或运行时 DLL 不兼容的问题。阶段 1 优先保证能编译、能运行，所以沿用原环境。

### 12. 播放器和录制器整合在一个 Qt 客户端中可能遇到哪些线程问题？

播放器有 `VideoPlayer` 读取线程、SDL 音频回调线程、视频解码线程。录制器有采集线程、音频采集回调、编码写文件线程。这些线程同时存在时，最容易遇到资源释放顺序、UI 跨线程更新、关闭窗口时线程未退出等问题。

阶段 1 的做法是尽量保留原有线程模型，只在 UI 嵌入和停止保护上做小改动。比如 `RecorderDialog` 增加 `m_isRecording`，避免未开始就停止；析构时如果仍在录制，会先关闭并等待线程。

### 13. 录制文件为什么可以直接交给播放页播放？

录制器输出的是标准媒体文件，封装为 FLV，视频是 H.264，音频是 AAC。播放器底层使用 FFmpeg 打开本地媒体文件，只要 FFmpeg 能识别该封装和编码格式，就可以像播放普通视频一样播放录制结果。

这也是阶段 1 的重要闭环：录制器生成文件，播放器打开文件，说明本地音视频能力已经打通。

### 14. 阶段 1 的局限性是什么？

阶段 1 只完成本地客户端整合，没有网络通信，没有服务端，没有登录，没有上传下载，也没有远程媒体列表。设置页也只是占位。

另外，录制仍依赖本机设备和权限，比如摄像头、麦克风、桌面采集权限；多屏和高 DPI 场景还需要后续专门适配。

### 15. 阶段 2 准备如何扩展？

阶段 2 建议先做最小 C/S 连通性，不直接做文件上传下载。客户端在 `AVClient` 中新增网络模块，设置页增加 IP、端口、连接、断开、Ping 按钮。服务端新建最小 `AVServer`，复用 NetDisk 服务端的 epoll 和线程池思路。

第一步协议只做 `PING_RQ / PING_RS`，目标是打通 Windows 客户端到 Ubuntu 服务端的 TCP 收发链路。等这个稳定后，再进入媒体列表、上传和下载。
