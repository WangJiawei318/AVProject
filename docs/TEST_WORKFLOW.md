# AVProject 完整测试流程

## 1. 测试前准备

- Ubuntu IP：`192.168.44.130`
- 服务端端口：`8000`
- `AVServer/media/` 至少准备一个受支持媒体
- Windows 摄像头、麦克风按录制需要授权

## 2. 启动服务端

```bash
cd ~/AVProject/AVServer
make clean
make
./AVServer 8000
```

确认打印：

```text
AVServer started
media directory: media
upload temp directory: temp
epoll LT reactor initialized
eventfd initialized
core worker count=4
max worker count=8
task queue capacity=256
listening on port 8000
```

还应看到上传任务目录和 4 个核心 worker 的创建日志。若有未完成任务，日志会显示恢复任务及安全偏移。后续连接和协议日志应带有 `fd=...`、`connectionId=...` 和协议类型。

## 3. 启动并连接客户端

1. 启动 `AVClient/bin/AVClient.exe`。
2. 打开 Settings。
3. 输入 `192.168.44.130` 和 `8000`。
4. 点击 Connect。
5. 确认状态为 Connected。

## 4. 功能测试

### 4.1 Ping

点击 Send Ping，确认客户端收到 Pong，服务端打印 `PING_RQ/PING_RS`。

### 4.2 本地播放

Player 页打开本地媒体，验证播放、暂停、恢复、停止和 seek。

### 4.3 本地录制

Recorder 页开始录制数秒后停止，确认 `AVClient/bin/recordings/` 生成 FLV，并能在 Player 页播放。

### 4.4 远程列表

Remote Media 页点击 Refresh media list，确认列表与 `AVServer/media/` 一致。

### 4.5 上传

点击 Select and upload file，选择支持格式，确认进度到 100%、服务端 `media/` 出现文件、列表自动刷新。

### 4.6 下载

选择远程文件，点击 Download file，确认：

- 进度到 100%；
- `AVClient/cache/` 出现正式文件；
- 文件大小与服务端一致；
- Player 页可手动打开。

### 4.7 下载并播放

选择远程文件，点击 Download and play，确认下载完成后自动切换 Player 页并开始播放。

## 5. 完整闭环

```text
本地录制或准备媒体
  -> 上传到 AVServer/media/
  -> 刷新远程列表
  -> 选择刚上传的文件
  -> 下载到 AVClient/cache/
  -> 自动切换并播放
```

该流程全部通过即可完成当前版本核心演示。

## 6. 多客户端并发测试

先运行自动化 Ping 和列表测试：

```bash
cd ~/AVProject
python3 tools/concurrent_client_test.py 192.168.44.130 8000 5 --pings 10 --media-list
```

确认 5 个连接均成功且 `failed=0`。再启动至少 3 个 AVClient：A 上传文件，B 持续 Ping，C 刷新列表或下载；中途关闭 A，确认 B、C 仍可使用，并检查 A 的未完成 `.part` 与任务元数据被保留且任务已解除活动连接绑定。

## 7. 上传断点续传测试

自动化测试：

```bash
cd ~/AVProject
python3 tools/resumable_upload_test.py 192.168.44.130 8000 \
  --size-mb 8 --blocks-before-disconnect 20
```

人工重点验证五种场景：

1. **网络断开**：上传到 20% 至 50% 后断开，重连并点击“恢复上传”，确认进度不是从 0 开始。
2. **客户端重启**：中途关闭 AVClient，重启后确认未完成任务仍显示，连接原服务器后恢复完成。
3. **服务端重启**：中途停止 AVServer，重新启动并确认加载任务日志，客户端重连后从服务端偏移继续。
4. **非法恢复**：使用错误 token 请求恢复，服务端拒绝且原 `.part` 大小不变。
5. **多客户端竞争**：A 恢复后，B 使用同一任务凭据请求恢复；B 收到 `task already active`，A 可继续。

完成后确认 `temp/<transfer_id>.part` 和 `temp/tasks/<transfer_id>.task` 已删除，正式文件进入 `media/`，媒体列表自动刷新。修改本地源文件大小或最后修改时间后，AVClient 应拒绝直接恢复。

## 8. 下载断点续传测试

自动化测试：

```bash
cd ~/AVProject
python3 tools/resumable_download_test.py 192.168.44.130 8000 test.mp4 \
  --blocks-before-disconnect 4 --output-dir ./download-test
```

人工重点验证四种场景：

1. **网络断开**：下载到 20% 至 50% 后断开，确认 `.part` 和状态保留；重连后点击“恢复下载”，确认从非零进度继续。
2. **客户端重启**：中途关闭并重启 AVClient，确认未完成下载仍显示，连接原服务器后恢复。
3. **服务端重启**：中途重启 AVServer，客户端重连并恢复；服务端不需要下载任务元数据。
4. **远程文件变化**：部分下载后替换服务端同名文件，恢复应返回 `remote file changed`，旧 `.part` 不得追加新数据。

完成后确认 `.part` 改为正式 cache 文件、下载状态已删除；“下载并播放”任务恢复完成后应自动切换 Player 页。放弃任务应删除本地状态和 `.part`。

## 9. 动态业务线程池测试

自动化测试：

```bash
cd ~/AVProject
python3 tools/thread_pool_concurrency_test.py \
  192.168.44.130 8000 \
  --clients 10 --requests 20
```

确认脚本输出 `failed=0`。如服务端已有 `test.mp4`，可增加 `--download-file test.mp4 --download-blocks 4` 验证并发分片读取。人工回归六类场景：

1. **基础多客户端**：A 上传，B 持续 Ping，C 刷新列表并下载，三者均正常。
2. **并行文件操作**：A 上传、B 下载、C 扫描列表，日志显示不同 `connectionId` 被不同 worker 执行。
3. **任务期间断开**：在 A 的业务任务执行时关闭 A，确认 completion 被丢弃，B、C 不受影响。
4. **续传回归**：分别中断并恢复上传和下载，确认权威 offset、`.part` 与状态文件逻辑不回退。
5. **关闭服务端**：存在连接和任务时发送 SIGINT/SIGTERM，确认停止接收任务、排空已入队任务、join worker 后退出。
6. **动态扩缩容**：启动为 4 个核心线程；增加并发业务使线程逐步扩到最多 8 个；任务结束并空闲约 60 秒后恢复到 4 个；再次 Ping、上传和下载仍正常。

队列上限为 256。若通过足够高的并发制造过载，应看到 `task rejected: queue full`，客户端收到 `server busy`，而连接的 in-flight 状态不能永久卡住。

## 10. 常见异常测试

| 场景 | 预期结果 |
| --- | --- |
| 服务端未启动 | Connect 失败，客户端不崩溃 |
| 未连接时刷新 | 提示先连接，不能发送列表请求 |
| 未连接时上传/下载 | 弹出连接提示 |
| 上传不支持格式 | 客户端拒绝，服务端也有白名单 |
| 下载未选择文件 | 提示先选择远程媒体 |
| `media/` 为空 | 列表为空并记录日志 |
| 上传中断开连接 | 客户端显示等待恢复，服务端保留 `.part` 与任务元数据 |
| 下载中断开连接 | 客户端保留 `.part` 和状态，任务显示等待恢复 |
| 恢复时服务器地址不匹配 | 拒绝发送，提示连接任务所属服务器 |
| 恢复时远程文件版本变化 | 返回 `remote file changed`，不继续写入 |
| 上传同名文件 | 服务端自动生成 `_1`、`_2` 名称 |
| cache 已有同名文件 | 完整下载后用新文件替换 |
| 一个客户端发送非法包长 | 只关闭该连接，其他客户端继续工作 |
| 一个客户端长期不读取响应 | 该连接发送队列达到上限后被关闭 |
| 业务线程池队列已满 | 当前请求收到 `server busy`，其他连接继续工作 |
| worker 完成前连接已关闭 | Reactor 按 `connectionId` 丢弃陈旧结果，不向复用 fd 发送 |

## 11. 测试记录建议

演示或提交前记录：

- 客户端和服务端 commit；
- Qt/MinGW 与 Ubuntu/g++ 版本；
- 测试文件名和大小；
- Ping、列表、上传、下载结果；
- 并发工具成功数、失败数和耗时；
- 是否完成下载后自动播放；
- 已知问题。
