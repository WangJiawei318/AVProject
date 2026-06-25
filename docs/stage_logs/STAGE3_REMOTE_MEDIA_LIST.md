# 阶段 3 开发记录：远程媒体列表协议

## 1. 本阶段目标

阶段 3 只实现“远程媒体列表”这一条最小闭环：`AVServer` 扫描服务端本地 `media/` 目录，`AVClient` 发送 `MEDIA_LIST_RQ` 请求，服务端返回 `MEDIA_LIST_RS`，客户端在远程媒体页用表格显示文件名、大小、修改时间和类型。

本阶段不做上传、下载、远程播放、删除、重命名、MySQL，也不实现 TCP 边下边播。这样可以先把“客户端请求服务端资源目录并展示”的协议链路跑通，为后续上传下载打基础。

## 2. 已完成内容

- `AVServer` 新增 `media/` 目录管理逻辑，启动时会确认目录存在，不存在则创建。
- `AVServer` 新增媒体目录扫描类 `MediaManager`。
- 支持扩展名过滤：`.mp4`、`.flv`、`.avi`、`.mkv`、`.mov`、`.wmv`、`.mp3`、`.aac`、`.wav`。
- 客户端和服务端协议头新增 `MEDIA_LIST_RQ / MEDIA_LIST_RS`。
- `MEDIA_LIST_RS` 使用文本 payload，每行格式为：`filename|filesize|mtime|extension`。
- `AVNetworkClient` 新增 `sendMediaListRequest()` 和 `mediaListReceived(QString)`。
- `MainWindow` 统一持有一个共享 `AVNetworkClient`，供设置页和远程媒体页共同使用。
- `AVClient` 新增 `RemoteMediaPage`，包含连接状态、刷新按钮、远程媒体表格和日志区。
- `AVClient.pro` 已加入远程媒体页源码。
- `AVServer/Makefile` 已加入 `src/MediaManager.cpp`。

## 3. 新增文件

- `AVClient/pages/RemoteMediaPage.h`
- `AVClient/pages/RemoteMediaPage.cpp`
- `AVServer/include/MediaManager.h`
- `AVServer/src/MediaManager.cpp`
- `docs/stage_logs/STAGE3_REMOTE_MEDIA_LIST.md`

## 4. 修改文件

- `AVClient/AVClient.pro`
- `AVClient/MainWindow.h`
- `AVClient/MainWindow.cpp`
- `AVClient/modules/network/av_protocol.h`
- `AVClient/modules/network/AVNetworkClient.h`
- `AVClient/modules/network/AVNetworkClient.cpp`
- `AVClient/pages/SettingsPage.h`
- `AVClient/pages/SettingsPage.cpp`
- `AVServer/Makefile`
- `AVServer/include/av_protocol.h`
- `AVServer/include/AVServer.h`
- `AVServer/src/AVServer.cpp`

## 5. 核心类与函数

### `MediaManager`

服务端媒体目录管理类，职责如下：

- `ensureMediaDir()`：确保 `media/` 目录存在。
- `buildMediaListPayload()`：扫描媒体目录并生成文本 payload。
- `isSupportedMediaFile()`：按扩展名过滤媒体文件。
- `extensionOf()`：提取并转小写扩展名。
- `sanitizeField()`：避免文件名中的 `|`、换行符破坏文本协议。

### `AVServer::sendMediaList()`

收到 `MEDIA_LIST_RQ` 后调用，流程如下：

1. 调用 `MediaManager::buildMediaListPayload()` 扫描媒体目录。
2. 生成 `STRU_MEDIA_LIST_RS_HEADER`，写入 payload 字节数。
3. 拼接 `header + payload`。
4. 复用阶段 2 的 `sendPacket()`，发送 `4 字节长度头 + 包体`。

### `AVNetworkClient`

新增能力：

- `sendMediaListRequest()`：发送 `MEDIA_LIST_RQ`。
- `onPacketReceived()`：解析 `MEDIA_LIST_RS`，读取 payload 并转成 UTF-8 文本。
- `mediaListReceived(QString)`：把媒体列表文本通知 UI。

### `RemoteMediaPage`

远程媒体页，职责如下：

- 显示当前连接状态。
- 未连接时禁用刷新按钮，并在日志中提示先连接服务器。
- 已连接时点击刷新发送 `MEDIA_LIST_RQ`。
- 收到 `MEDIA_LIST_RS` 后解析 payload，填充 `QTableWidget`。

## 6. 协议设计

通信仍沿用阶段 2 的基础帧格式：

```text
4 字节包体长度 + 协议包体
```

新增协议号：

```cpp
DEF_PACK_MEDIA_LIST_RQ = DEF_PACK_BASE + 5
DEF_PACK_MEDIA_LIST_RS = DEF_PACK_BASE + 6
```

请求包：

```cpp
struct STRU_MEDIA_LIST_RQ
{
    PackType type;
};
```

响应包头：

```cpp
struct STRU_MEDIA_LIST_RS_HEADER
{
    PackType type;
    int32_t payloadSize;
};
```

响应 payload：

```text
filename|filesize|mtime|extension
demo.mp4|102400|2026-06-25 16:30:01|mp4
record.flv|204800|2026-06-25 16:31:12|flv
```

本阶段选择文本 payload 的原因是调试简单、便于打印、便于在面试中解释。缺点是字段转义能力有限，后续如果协议复杂，可以升级为二进制 TLV、JSON 或 protobuf。

## 7. 通信流程

```text
用户在 Settings 页连接服务器
        |
        v
MainWindow 中共享的 AVNetworkClient 保持 TCP 连接
        |
        v
用户进入 Remote Media 页并点击 Refresh
        |
        v
RemoteMediaPage 调用 AVNetworkClient::sendMediaListRequest()
        |
        v
TcpClient 发送 4 字节长度头 + MEDIA_LIST_RQ
        |
        v
AVServer::handlePacket() 分发到 sendMediaList()
        |
        v
MediaManager 扫描 AVServer/media/
        |
        v
AVServer 返回 MEDIA_LIST_RS_HEADER + 文本 payload
        |
        v
AVNetworkClient 解析响应并发出 mediaListReceived()
        |
        v
RemoteMediaPage 填充表格
```

## 8. 构建方法

### Windows 客户端

```powershell
cd D:\colin\project\AVProject\AVClient\build-debug

$env:PATH='D:\Software\Qt\Tools\mingw730_32\bin;D:\Software\Qt\5.12.11\mingw73_32\bin;' + $env:PATH
D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe ..\AVClient.pro -spec win32-g++ "CONFIG+=debug" "CONFIG+=qml_debug"
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe -j4
```

可选：补齐运行时 DLL。

```powershell
cd D:\colin\project\AVProject
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\AVClient\deploy\copy_runtime_dlls.ps1
```

运行：

```powershell
D:\colin\project\AVProject\AVClient\bin\AVClient.exe
```

### Ubuntu 服务端

把最新代码同步到 Ubuntu 后执行：

```bash
cd AVProject/AVServer
make clean
make
./AVServer 8000
```

首次启动后会自动创建：

```text
AVServer/media/
```

也可以手动创建并放入测试文件：

```bash
mkdir -p media
cp ~/Videos/test.mp4 media/
cp ~/Videos/test.flv media/
```

## 9. 测试步骤

1. 在 Ubuntu 启动服务端：

```bash
cd AVProject/AVServer
make
./AVServer 8000
```

2. 确认服务端输出：

```text
server started
media directory: media
listening on port 8000
```

3. 在 `AVServer/media/` 放入若干测试媒体文件。

4. 在 Windows 启动 `AVClient`。

5. 进入 `Settings` 页，连接 `192.168.44.130:8000`，并用 `Ping` 确认阶段 2 链路仍正常。

6. 进入 `Remote Media` 页，点击 `Refresh media list`。

7. 预期客户端表格显示：

- File name
- Size
- Modified time
- Type

8. 预期服务端日志出现：

```text
received MEDIA_LIST_RQ
scan media directory
media count: N
sent MEDIA_LIST_RS
```

9. 如果 `media/` 为空，客户端日志应显示 `server media directory is empty`。

## 10. 常见问题与排查

### Remote Media 页提示未连接

先去 `Settings` 页连接服务器。阶段 3 已把 `AVNetworkClient` 提升为 `MainWindow` 共享对象，所以连接状态会在设置页和远程媒体页之间共享。

### 客户端能 Ping，但媒体列表为空

检查文件是否放在 `AVServer/media/`，并确认扩展名属于支持列表。注意当前服务端的 `media/` 是相对目录，通常是从 `AVServer` 目录运行 `./AVServer` 时的 `AVServer/media/`。

### 服务端没有收到 `MEDIA_LIST_RQ`

确认客户端切换到 `Remote Media` 页后点击了刷新按钮。再检查客户端日志是否出现 `sent MEDIA_LIST_RQ`。

### 收到列表但表格字段错位

检查文件名中是否含有 `|` 或换行。本阶段已将这些字符替换为 `_`，如果后续支持更复杂文件名，建议升级 payload 编码格式。

### Ubuntu 编译失败提示找不到新增文件

确认 `AVServer/Makefile` 中包含：

```makefile
SOURCES := src/main.cpp src/AVServer.cpp src/MediaManager.cpp
```

并确认 `include/MediaManager.h`、`src/MediaManager.cpp` 已同步到 Ubuntu。

### Windows 客户端编译后页面没有变化

重新执行 qmake，不要只执行 make。因为 `.pro` 新增了 `RemoteMediaPage`，旧 Makefile 不会自动完整更新。

## 11. 本阶段未做内容

- 不做文件上传。
- 不做文件下载。
- 不做远程播放。
- 不做删除、重命名。
- 不做 MySQL。
- 不做服务端媒体元数据缓存。
- 不做大目录分页。
- 不做 TCP 边下边播。
- 不改播放器和录制器核心逻辑。

## 12. 下一阶段建议

下一阶段建议进入“下载到本地 cache 后播放”的准备工作，先不要直接做边下边播：

1. 设计 `MEDIA_DOWNLOAD_RQ / MEDIA_DOWNLOAD_RS / MEDIA_DOWNLOAD_DATA`。
2. 客户端新增 `cache/` 目录管理。
3. 从远程媒体表格选择一个文件。
4. 点击下载，保存到本地 cache。
5. 下载完成后调用现有播放器打开本地文件。

上传可以放在再下一步：录制完成后，把输出文件通过分片上传到服务端 `media/`，再刷新远程媒体列表。

## 13. 面试问答准备

### 1. 为什么阶段 3 只做媒体列表？

媒体列表是上传下载之前最小的资源管理闭环。它能验证客户端请求、服务端目录扫描、协议响应、UI 展示和连接状态共享。如果这个闭环稳定，后续上传下载只是扩展协议和文件 IO。

### 2. 为什么不直接远程播放？

第一版远程播放如果做边下边播，会同时涉及网络缓冲、播放器 demux 阻塞、seek、缓存命中和异常恢复，风险很高。当前路线选择先下载到本地 cache，再调用已有播放器播放，能复用阶段 1 的稳定能力。

### 3. 为什么 `MEDIA_LIST_RS` 用文本格式？

文本格式便于调试，服务端日志和抓包都容易看懂，适合第一版。缺点是字段转义和扩展性有限，后续功能复杂后可以换成 JSON、TLV 或 protobuf。

### 4. 为什么服务端使用 `media/` 相对目录？

相对目录便于部署和演示，不需要额外配置文件。只要从 `AVServer` 目录启动，媒体文件就在 `AVServer/media/`，路径清晰。后续可以升级为配置项。

### 5. 为什么要过滤扩展名？

避免把非媒体文件、临时文件和隐藏文件暴露给客户端。第一版只按扩展名过滤，简单可控。后续可以用 FFmpeg 探测真实封装格式。

### 6. 为什么 `MainWindow` 要共享 `AVNetworkClient`？

阶段 2 中网络对象只在设置页里，远程媒体页无法复用同一条连接。把网络对象提升到 `MainWindow` 后，设置页负责连接，远程媒体页负责业务请求，两个页面共享连接状态。

### 7. 如何处理 TCP 半包和粘包？

仍然使用阶段 2 的 4 字节长度头。接收端先读取固定长度的包体长度，再循环读取完整包体，然后根据包体第一个字段 `PackType` 分发协议。

### 8. 服务端为什么暂时不是 epoll？

当前仓库阶段 2 的 `AVServer` 是最小阻塞式 server，适合先验证协议。最终目标仍然可以参考 `NetDisk-Server` 改造成 epoll + 线程池 + 协议分发，但不应该和媒体列表协议混在同一次大改里。

### 9. 列表很大怎么办？

当前限制单包最大约 1 MB，目录很大时会截断。后续可以做分页协议，例如 `offset/count`，或者服务端返回多包分片。

### 10. 文件修改时间在哪一端格式化？

当前在服务端格式化为 `YYYY-MM-DD HH:MM:SS`，客户端直接展示。这样 UI 简单。后续如果需要时区或排序，可以传 Unix timestamp，由客户端格式化。

### 11. 为什么不用 MySQL 保存媒体列表？

第一版目录扫描更直接，不需要数据库部署和表结构。等上传下载稳定后，再考虑用 MySQL 保存用户、媒体元数据、上传记录和权限。

### 12. 这阶段对简历项目有什么价值？

它把本地音视频能力和 C/S 架构连接起来：客户端通过自定义 TCP 协议请求服务端媒体资源，服务端扫描媒体目录并返回结构化列表，客户端通过 Qt UI 展示。这是后续“录制上传、远程媒体管理、下载播放”的基础。
