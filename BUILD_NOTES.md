# AVProject 构建与依赖记录

## 当前扫描结果

本轮未执行完整编译，只做了目录、工程文件和源码审查。当前 `AVProject` 不是 Git 仓库，`git status` 返回 `not a git repository`。

## MediaPlayer

- 工程文件：`MediaPlayer/MediaPlayer.pro`
- 生成目录：`MediaPlayer/build-debug`
- 已有产物：`MediaPlayer/build-debug/debug/MediaPlayer.exe`
- Qt Kit：从 Makefile 看是 `D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe`
- mkspec：`win32-g++`
- Qt 模块：`core gui widgets opengl`
- C++ 标准：C++11
- 第三方库：
  - FFmpeg 4.2.2：`MediaPlayer/ffmpeg-4.2.2`
  - SDL2 2.0.10：`MediaPlayer/SDL2-2.0.10`
- 运行 DLL 已在 `MediaPlayer/build-debug/debug` 中存在。

注意：

- `.pro` 使用 `SDL2-2.0.10/lib/x86/SDL2.lib`。
- 整合后统一 `main.cpp` 仍要保留 `SDL_MAIN_HANDLED` 和 `SDL_SetMainReady()`。

## VideoRecorder

- 工程文件：`VideoRecorder/VideoRecorder.pro`
- 生成目录：`VideoRecorder/bulid-debug`
- 已有产物：`VideoRecorder/bulid-debug/debug/VideoRecorder.exe`
- Qt Kit：从 Makefile 看是 `D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe`
- mkspec：`win32-g++`
- Qt 模块：`core gui widgets multimedia`
- C++ 标准：C++11
- 第三方库：
  - FFmpeg 4.2.2：`VideoRecorder/ffmpeg-4.2.2`
  - OpenCV 4.2.0：`VideoRecorder/opencv-release`

明显问题：

- `VideoRecorder.pro` 的 OpenCV include/lib 路径写死到旧目录：
  - `D:\colin\project\VideoPlayer\0602\VideoRecorder\opencv-release/include/opencv2`
  - `D:\colin\project\VideoPlayer\0602\VideoRecorder\opencv-release/include`
  - `D:\colin\project\VideoPlayer\0602\VideoRecorder\opencv-release\lib\libopencv_*.dll.a`
- 第一阶段应改为当前项目内相对路径：
  - `$$PWD/opencv-release/include/opencv2`
  - `$$PWD/opencv-release/include`
  - `$$PWD/opencv-release/lib/libopencv_*.dll.a`

## NetDisk-Client

- 工程文件：`NetDisk-Client/NetDisk.pro`
- 网络子工程：`NetDisk-Client/netapi/netapi.pri`
- 生成目录：`NetDisk-Client/build-debug`
- 已有产物：`NetDisk-Client/build-debug/debug/NetDisk.exe`
- Qt Kit：从 Makefile 看是 `D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe`
- mkspec：`win32-g++`
- Qt 模块：`core gui widgets network`
- 依赖：Winsock、Mswsock、pthread 链接项。

可复用重点：

- `netapi/net/TcpClient.*`
- `netapi/net/INet.*`
- `netapi/mediator/INetMediator.*`
- `netapi/mediator/TcpClientMediator.*`

注意：

- `CKernel` 中 IP 默认值和实际调用硬编码为 `192.168.44.130`。
- `loadInitFile()` 中读取配置使用 `prot`，写入使用 `port`，存在 key 不一致。

## NetDisk-Server

- 工程文件：`NetDisk-Server/NetDisk.pro`
- Makefile：`NetDisk-Server/src/makefile`
- 入口：`NetDisk-Server/src/main.cpp`
- 平台：Ubuntu/Linux
- 编译器：g++，`-std=gnu++11`
- 依赖：`pthread`、`mysqlclient`

可复用重点：

- `include/block_epoll_net.h` + `src/block_epoll_net.cpp`
- `include/Thread_pool.h` + `src/Thread_pool.cpp`
- `include/TCPKernel.h` + `src/TCPKernel.cpp`
- `include/clogic.h` + `src/clogic.cpp`
- `include/packdef.h`

明显问题：

- `TcpKernel::Open()` 当前强制连接 MySQL，数据库失败则服务启动失败。
- 数据库账号密码硬编码在 `packdef.h`。
- 当前协议仍是网盘/登录注册示例，不应直接作为 AV 业务协议。

## 第一阶段建议统一环境

- Windows：Qt 5.12.11 MinGW 7.3.0 32-bit
- FFmpeg：继续使用项目内 4.2.2
- SDL：继续使用项目内 SDL2 2.0.10 x86
- OpenCV：继续使用项目内 OpenCV 4.2.0 MinGW 版本
- 服务端：Ubuntu + g++ + make + pthread，MySQL 放到阶段 3 再处理
