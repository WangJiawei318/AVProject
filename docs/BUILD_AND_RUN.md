# AVProject 构建与运行

## 1. 环境概览

| 程序 | 平台 | 构建方式 |
| --- | --- | --- |
| `AVClient` | Windows | Qt 5.12.11 + MinGW 7.3.0 32-bit + qmake |
| `AVServer` | Ubuntu | g++ + GNU make |

客户端第三方库必须与 32-bit MinGW Kit 匹配。不要把 MSVC、64-bit 和 MinGW 32-bit 的库混用。

## 2. Windows 客户端依赖

### 2.1 Qt 与编译器

当前验证环境：

```text
Qt:       5.12.11
Kit:      Desktop Qt 5.12.11 MinGW 32-bit
Compiler: MinGW 7.3.0 32-bit
qmake:    D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe
make:     D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe
```

Qt 模块：

```text
core gui widgets multimedia opengl network
```

### 2.2 FFmpeg

版本：FFmpeg 4.2.2。

`AVClient.pro` 当前复用：

```text
MediaPlayer/ffmpeg-4.2.2/include
MediaPlayer/ffmpeg-4.2.2/lib
```

链接模块包括：

```text
avcodec avdevice avfilter avformat avutil
postproc swresample swscale
```

程序运行时还需要对应 FFmpeg DLL。

### 2.3 SDL

版本：SDL2 2.0.10 x86。

用途：

- 播放音频；
- 播放器 packet 队列同步；
- 视频解码线程和纯视频定时。

`main.cpp` 和工程保留了 `SDL_MAIN_HANDLED` 相关初始化。

### 2.4 OpenCV

版本：OpenCV 4.2.0 MinGW 版本。

当前路径：

```text
VideoRecorder/opencv-release/include
VideoRecorder/opencv-release/lib
```

录制模块还使用 Qt Screen API 和 Qt Multimedia。

## 3. Windows 命令行构建

### 3.1 创建构建目录

```powershell
cd D:\colin\project\AVProject\AVClient
New-Item -ItemType Directory -Force build-debug
cd build-debug
```

### 3.2 运行 qmake

```powershell
D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe ..\AVClient.pro `
  -spec win32-g++ "CONFIG+=debug" "CONFIG+=qml_debug"
```

如果只需要普通 Debug 构建，可以省略 `CONFIG+=qml_debug`。

### 3.3 编译

```powershell
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe -j4
```

输出程序：

```text
AVClient/bin/AVClient.exe
```

### 3.4 清理后重编

修改协议头、Qt signal/slot 或 `.pro` 后，建议：

```powershell
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe clean
D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe ..\AVClient.pro -spec win32-g++ "CONFIG+=debug"
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe -j4
```

## 4. Qt Creator 构建

1. 用 Qt Creator 打开 `AVClient/AVClient.pro`。
2. 选择 Desktop Qt 5.12.11 MinGW 32-bit Kit。
3. 将 Build directory 设置为 `AVClient/build-debug` 或其他被 Git 忽略的目录。
4. 执行 Run qmake。
5. Build Project。
6. 运行时工作目录建议使用 `AVClient/bin`。

若 Qt Creator 仍显示旧 UI 或旧协议，先 Run qmake，再 Rebuild Project。

## 5. DLL 部署

脚本：

```text
AVClient/deploy/copy_runtime_dlls.ps1
```

执行：

```powershell
cd D:\colin\project\AVProject
powershell -ExecutionPolicy Bypass -File .\AVClient\deploy\copy_runtime_dlls.ps1
```

脚本执行内容：

1. 从 `MediaPlayer/dll/` 复制播放器依赖；
2. 从 `VideoRecorder/dll/` 复制录制器依赖；
3. 复制 MinGW runtime DLL；
4. 若 `AVClient.exe` 已存在，调用 `windeployqt --debug`；
5. 将运行库放到 `AVClient/bin/`。

脚本中的 Qt 和 MinGW 路径与当前开发机一致。换机器后需要修改：

```powershell
$mingwBin
$qtDeploy
```

常见运行 DLL：

```text
Qt5Core[d].dll
Qt5Gui[d].dll
Qt5Widgets[d].dll
Qt5Multimedia[d].dll
Qt5Network[d].dll
FFmpeg DLL
SDL2.dll
OpenCV DLL
libgcc_s_dw2-1.dll
libstdc++-6.dll
libwinpthread-1.dll
platforms/qwindows[d].dll
```

Debug 程序通常需要带 `d` 的 Qt DLL，Release 程序需要不带 `d` 的 DLL，不要混用。

## 6. 客户端运行目录

```text
AVClient/
├── bin/
│   ├── AVClient.exe
│   ├── *.dll
│   ├── platforms/
│   └── recordings/       # 默认录制输出
├── cache/                # 下载完成的媒体
│   └── *.part            # 可恢复下载半成品
├── transfer_state/       # 未完成上传/下载任务 JSON
└── build-debug/          # 编译中间文件
```

这些运行产物由 `.gitignore` 排除。

## 7. Ubuntu 服务端环境

推荐 Ubuntu 安装基础工具：

```bash
sudo apt update
sudo apt install build-essential default-libmysqlclient-dev libsodium-dev mysql-server
```

确认：

```bash
g++ --version
make --version
mysql_config --version
pkg-config --modversion libsodium
```

`AVServer` 不依赖 Qt、FFmpeg、SDL 或 OpenCV，但阶段 11 需要 MySQL C API 开发包和 libsodium。Ubuntu/Debian 使用 `default-libmysqlclient-dev` 可获得 `mysql_config` 与兼容客户端头库；`libsodium-dev` 提供 Argon2id 密码哈希接口。

### 7.1 初始化数据库

先编辑 `sql/000_create_database.sql.example`，把两处 `CHANGE_ME` 改为同一个强密码，再执行：

```bash
cd ~/AVProject/AVServer
sudo mysql < sql/000_create_database.sql.example
mysql -u avapp -p avproject < sql/001_init_auth_media.sql
cp config/db.conf.example config/db.conf
chmod 600 config/db.conf
```

编辑 `config/db.conf`，写入刚才的应用密码。AVServer 只连接 `127.0.0.1:3306`，不使用 root；示例账号只拥有 `avproject` 上的 SELECT、INSERT、UPDATE 权限。真实 `db.conf` 已由 `.gitignore` 排除。

确认表：

```bash
mysql -u avapp -p avproject -e "SHOW TABLES; DESCRIBE users; DESCRIBE media;"
```

## 8. Ubuntu 服务端构建

确保当前阶段的完整 `AVServer/` 已同步到 Ubuntu，尤其包括：

```text
AVServer/include/av_protocol.h
AVServer/include/AVServer.h
AVServer/include/ConnectionContext.h
AVServer/include/EpollServer.h
AVServer/include/ThreadPool.h
AVServer/include/ProtocolDispatcher.h
AVServer/include/DatabaseConnectionPool.h
AVServer/include/AuthService.h
AVServer/include/MediaRepository.h
AVServer/include/MediaManager.h
AVServer/include/UploadManager.h
AVServer/include/DownloadManager.h
AVServer/src/AVServer.cpp
AVServer/src/ConnectionContext.cpp
AVServer/src/EpollServer.cpp
AVServer/src/ThreadPool.cpp
AVServer/src/ProtocolDispatcher.cpp
AVServer/src/DatabaseConnectionPool.cpp
AVServer/src/AuthService.cpp
AVServer/src/MediaRepository.cpp
AVServer/src/MediaManager.cpp
AVServer/src/UploadManager.cpp
AVServer/src/DownloadManager.cpp
AVServer/Makefile
AVServer/config/db.conf
```

然后执行：

```bash
cd ~/AVProject/AVServer
make clean
make
```

输出：

```text
AVServer/AVServer
```

启动：

```bash
./AVServer 8000
```

不传端口时也默认使用 8000：

```bash
./AVServer
```

阶段 11 启动后应看到类似日志：

```text
database pool connected host=127.0.0.1 port=3306 database=avproject size=4
media directory: media
upload temp directory: temp
upload task directory: temp/tasks
AVServer started
epoll LT reactor initialized
eventfd initialized
core worker count=4
max worker count=8
task queue capacity=256
non-core idle timeout=60s
listening on port 8000
```

`Makefile` 使用 `-pthread`、`mysql_config --cflags/--libs` 和 `-lsodium`。客户端连接、任务提交和完成日志会同时包含 `fd=...`、`connectionId=...` 与协议类型。若存在未完成上传任务，日志会显示 transfer ID，但不会打印完整 resume token、密码、password hash 或数据库密码。

## 9. 服务端运行目录

服务端使用相对路径。应从 `AVServer/` 目录启动：

```text
AVServer/
├── AVServer             # Linux 可执行文件
├── config/db.conf       # 本机数据库凭据，不提交
├── media/               # 正式远程媒体
└── temp/
    ├── *.part            # 上传中的文件数据
    └── tasks/*.task      # 可恢复任务元数据
```

- `media/`：启动时自动创建，只保存文件本体；媒体业务列表来自 MySQL `media` 表。
- `temp/`：启动时自动创建，上传未完成文件和任务元数据保存在这里。
- `temp/tasks/`：小型键值任务文件；不要手工修改 token、offset 或路径。
- `cache/`：属于 Windows 客户端，不在服务端。

如果从其他目录执行绝对路径，`media/` 和 `temp/` 会创建在当前 shell 工作目录，而不是可执行文件旁边。

同理，`config/db.conf` 是相对路径，因此必须从 `AVServer/` 启动。配置缺失或数据库初始连接失败时，程序打印明确错误并退出。

## 10. 查看 IP 地址

Ubuntu：

```bash
ip addr
```

或者：

```bash
hostname -I
```

当前虚拟机地址：

```text
192.168.44.130
```

若 IP 变化，应在客户端 Settings 页输入新地址。

Windows 查看本机和虚拟网卡：

```powershell
ipconfig
```

测试连通性：

```powershell
ping 192.168.44.130
```

Ping 命令可达不代表 TCP 8000 一定可达，还需要检查服务端和防火墙。

## 11. 查看端口监听

Ubuntu：

```bash
ss -lntp | grep 8000
```

或：

```bash
sudo lsof -iTCP:8000 -sTCP:LISTEN
```

Windows 测试 TCP：

```powershell
Test-NetConnection 192.168.44.130 -Port 8000
```

## 12. 自动化测试工具

阶段 11 认证与媒体列表烟测只使用 Python 标准库：

```bash
cd ~/AVProject
python3 tools/auth_media_test.py --host 127.0.0.1 --port 8000
```

脚本生成临时 userA/userB，验证未登录列表、重复注册、错误密码、SQL 注入式用户名、正确登录，以及 PUBLIC/MINE 基础查询。它不连接 MySQL，也不会打印生成的密码。当前长度头和结构体整数沿用主机小端格式，因此测试机与服务端应保持当前 x86/x86-64 小端环境。

并发 Ping 仍可使用：

```bash
python3 tools/concurrent_client_test.py 192.168.44.130 8000 5 --pings 10
```

阶段 11 后媒体业务要求先登录并且下载按 mediaId 请求。阶段 8/9 的独立续传脚本和阶段 10 的媒体并发参数保留为历史协议测试参考，不应直接对阶段 11 服务端运行；当前上传/下载续传和线程池媒体并发请通过两个已登录 AVClient 做人工回归。

阶段 8 上传断点续传脚本历史命令：

```bash
cd ~/AVProject
python3 tools/resumable_upload_test.py 192.168.44.130 8000 \
  --size-mb 8 --blocks-before-disconnect 20
```

工具会创建临时测试文件、上传若干块后主动断线、使用服务端返回的 transfer ID 与 token 重连恢复，并完成 FINISH。若测试脚本能直接访问服务端工作目录，还可增加：

```bash
python3 tools/resumable_upload_test.py 127.0.0.1 8000 \
  --server-media-dir ./AVServer/media
```

阶段 9 下载断点续传脚本历史命令：

```bash
cd ~/AVProject
python3 tools/resumable_download_test.py 192.168.44.130 8000 test.mp4 \
  --blocks-before-disconnect 4 --output-dir ./download-test
```

它会下载若干 64 KB 分片后主动断线，把 `.part` 和 JSON 状态写入输出目录，再以非零 offset 重连并完成下载。脚本验证 accepted offset 和最终文件大小，不播放文件。下载恢复不需要服务端任务目录；AVServer 仅重新检查远程文件大小、修改时间和请求偏移。

阶段 10 业务线程池并发脚本历史命令：

```bash
cd ~/AVProject
python3 tools/thread_pool_concurrency_test.py \
  192.168.44.130 8000 \
  --clients 10 --requests 20
```

脚本只使用 Python 标准库：同步启动多个连接、重复发送 Ping 和媒体列表请求、主动断开其中一个连接，并验证其余连接仍可继续。若要并发验证下载分片，可增加：

```bash
python3 tools/thread_pool_concurrency_test.py \
  192.168.44.130 8000 \
  --clients 10 --requests 20 \
  --download-file test.mp4 --download-blocks 4
```

这些历史脚本中的媒体包仍是对应阶段的旧结构。阶段 11 的当前验证以 `auth_media_test.py` 和两个已登录 AVClient 为准。启动时应有 4 个核心 worker；任务明显积压时观察 `thread pool expanded`，线程总数不得超过 8；任务结束并空闲约 60 秒后，应看到非核心 worker 因 `idle_timeout` 退出并恢复到 4 个。

## 13. 推荐启动顺序

1. Ubuntu 进入 `AVServer/`。
2. `make clean && make`。
3. `./AVServer 8000`。
4. Windows 启动 `AVClient/bin/AVClient.exe`。
5. Settings 页输入 IP 和端口。
6. Connect。
7. Send Ping。
8. Account 页 Register 后 Login。
9. Remote Media 页分别刷新 Public media 和 My media。

## 14. 常见构建问题

### 14.1 客户端链接库位数不匹配

表现：

```text
file format not recognized
undefined reference
无法启动，提示不是有效的 Win32 应用程序
```

检查 Qt、MinGW、FFmpeg、SDL、OpenCV 是否全部为 32-bit MinGW 兼容版本。

### 14.2 缺少 qwindows 插件

表现：

```text
could not find or load the Qt platform plugin "windows"
```

运行部署脚本或执行 `windeployqt`，确认：

```text
AVClient/bin/platforms/qwindowsd.dll
```

### 14.3 服务端提示 address already in use

检查已有进程：

```bash
ss -lntp | grep 8000
```

停止旧服务端，或临时换端口并在客户端同步修改。

### 14.4 服务端提示 unknown packet type: 20013

`20013` 是 `DOWNLOAD_INIT_RQ`。这说明客户端已经发出阶段 5 下载请求，但当前运行的服务端二进制没有阶段 5 分发逻辑。

处理：

```bash
cd ~/AVProject/AVServer
grep -n "DEF_PACK_DOWNLOAD_INIT_RQ" include/av_protocol.h src/ProtocolDispatcher.cpp
grep -n "DownloadManager" Makefile
make clean
make
./AVServer 8000
```

如果 grep 没有结果，先把当前 `AVServer/` 源码完整同步到 Ubuntu 并重新构建，避免仍在运行阶段 5 的旧二进制。

### 14.5 服务端列表为空

阶段 11 列表来自数据库。先确认登录成功，再检查：

```bash
pwd
ls -lah media
mysql -u avapp -p avproject -e "SELECT id,owner_user_id,original_name,stored_name,status FROM media;"
```

磁盘中存在但没有 `media` 记录的历史文件不会显示，这是避免错误归属的预期行为。

### 14.6 服务端目录无权限

```bash
ls -ld . media temp
```

运行用户需要对项目运行目录具有读写和重命名权限。

### 14.7 数据库配置或连接失败

若出现 `database config not found`，确认当前目录是 `AVServer/` 且存在权限为 600 的 `config/db.conf`。若出现 `cannot connect to MySQL`，依次检查：

```bash
sudo systemctl status mysql
ss -lntp | grep 3306
mysql -h 127.0.0.1 -u avapp -p avproject -e "SELECT 1;"
```

应用密码、用户 host 和数据库名必须与初始化脚本一致。MySQL 不需要对外网开放 3306。

### 14.8 链接阶段找不到 MySQL 或 libsodium

```bash
sudo apt install default-libmysqlclient-dev libsodium-dev
mysql_config --cflags --libs
ldconfig -p | grep sodium
make clean && make
```
