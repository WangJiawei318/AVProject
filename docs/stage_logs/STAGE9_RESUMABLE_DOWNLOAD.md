# 阶段 9：下载断点续传与客户端任务恢复

## 1. 阶段目标

本阶段在阶段 5 普通分片下载的基础上增加最小下载断点续传：网络断开、AVClient 退出或 AVServer 重启后，客户端仍可使用本地 `.part` 和任务状态继续下载。恢复成功后保持原有“下载”和“下载并播放”行为。

范围刻意保持简单：一次只允许一个活动下载，不加入线程池、SHA-256、多任务并行、自动重试、通用取消协议、用户系统或数据库。

## 2. 为什么下载续传比上传续传简单

上传的半成品位于服务端，服务端必须保存任务身份、token、已写偏移和当前连接所有权。下载的权威源文件本来就完整存在于 `media/`，每个 BLOCK 请求又携带文件名与 offset，因此服务端可以随时重新打开文件并定位读取。

下载恢复只需解决两个问题：客户端还可靠保存了多少字节，以及远程文件自上次 INIT 后是否变化。前者由本地状态和 `.part` 共同判断，后者由服务端文件大小与修改时间判断。

## 3. 为什么服务端下载保持无状态

`DownloadManager` 不保存 download ID、token、文件游标或下载任务表。每个 `DOWNLOAD_BLOCK_RQ` 都是独立请求：服务端验证文件名和 offset，打开 `media/<filename>`，seek 到 offset，最多读取 64 KB 后返回。

因此 AVServer 重启不会丢失下载会话状态，因为本来就没有必须恢复的下载会话。客户端重新发送 INIT 和后续 BLOCK 即可。服务端持有的只是当前请求期间的局部变量。

## 4. 下载恢复流程

```text
选择远程文件
  -> DOWNLOAD_INIT_RQ(offset=0, expected size/mtime=0)
  -> DOWNLOAD_INIT_RS(size, mtime, accepted_offset=0)
  -> 创建 download 状态与 cache/*.part
  -> 串行请求 DOWNLOAD_BLOCK
  -> 每块成功写入后保存 confirmed_offset

断线或 AVClient 退出
  -> 关闭文件
  -> 保留 .part 和 *.download.json

用户重连并点击恢复下载
  -> 计算 safe_offset
  -> DOWNLOAD_INIT_RQ(safe_offset, old size, old mtime)
  -> 服务端校验远端文件版本
  -> DOWNLOAD_INIT_RS(accepted_offset)
  -> 客户端截断/seek 到 accepted_offset
  -> 继续 BLOCK，完成后 FINISH
  -> .part 改名，删除状态
```

## 5. 本地 `.part` 文件

下载中的数据写入 `AVClient/cache/<filename>.part`。这个后缀明确表示文件尚未完成，播放器不会把它当正式媒体使用。断线时不再删除 `.part`，而是将它作为恢复数据保留。

只有收到全部字节、检查实际大小并完成 FINISH 后，客户端才把 `.part` 改为正式 cache 文件。

## 6. DownloadTaskStore

新增 `DownloadTaskStore`，负责：

- 确定 `AVClient/transfer_state/` 状态目录；
- 使用 `QSaveFile` 原子写入每个任务的 JSON；
- 启动时读取 `*.download.json`；
- 校验任务 ID、远程文件名和 cache 路径；
- 删除完成或被放弃的状态。

状态文件名为 `<task_id>.download.json`。每任务一文件便于独立提交和删除，不需要引入数据库或复杂任务中心。

## 7. 客户端状态字段

```text
task_id
remote_filename
local_part_path
local_final_path
server_ip
server_port
expected_file_size
expected_modified_time
confirmed_offset
play_after_download
status
```

`play_after_download` 使“下载并播放”在 AVClient 重启后仍保留原意。服务器地址用于阻止任务被错误地发往另一台服务器。

## 8. safe_offset 计算

恢复前不能只相信 JSON 中的 `confirmed_offset`。客户端同时读取 `.part` 实际大小：

```text
safe_offset = min(confirmed_offset, part_file_size)
```

- `.part` 大于 confirmed offset：截断到 confirmed offset，丢弃未确认尾部；
- `.part` 小于 confirmed offset：以实际大小为准，并回写状态；
- `.part` 不存在：任务不可直接恢复；
- 任一偏移为负或超过预期总大小：拒绝恢复。

这是保守策略，最多重复下载少量数据，不会跳过本地缺失区间。

## 9. accepted_offset 设计

客户端把 safe offset 发给服务端，但真正开始位置以 `DOWNLOAD_INIT_RS.accepted_offset` 为准。服务端只有在远端文件大小、修改时间和偏移都合法时才接受该值。

客户端收到响应后再次检查 `0 <= accepted_offset <= file_size`，将 `.part` 截断到该位置并 seek，然后请求下一块。当前服务端通常原样接受 safe offset，但保留 accepted offset 字段能让协议语义清楚，也便于未来增加块边界对齐策略。

## 10. 如何判断远程文件变化

新下载 INIT 返回当前 `file_size` 和 `modified_time`。恢复时客户端把这两个旧值带回服务端。服务端重新 `stat()` 文件并要求：

```text
current_file_size == expected_file_size
current_modified_time == expected_modified_time
resume_offset <= current_file_size
```

任一版本字段不同就返回 `remote file changed`，客户端保留旧任务但不继续写入，避免把两版文件拼接起来。

## 11. 为什么使用文件大小和修改时间

文件大小与 mtime 可以由 `stat()` 快速获得，不需要扫描整个大文件，适合当前单线程 Reactor。它们足以发现常见的替换、追加、截断和重新编码操作，也不需要引入新依赖。

## 12. 当前一致性校验的局限

大小与秒级修改时间不是内容哈希。如果文件内容被替换后大小相同，并且 mtime 被保留或碰巧相同，当前方案无法发现。下载完成只验证最终大小，不应声称具备强内容完整性保证。

后续应在工作线程中计算并比较 SHA-256，或为媒体文件提供稳定 file ID/ETag。TLS 解决传输窃听与篡改问题，SHA-256 解决内容一致性问题，两者职责不同。

## 13. 客户端重启恢复

AVClient 启动时加载全部合法 `*.download.json`，在 Remote Media 页的“未完成下载任务”表格显示文件名、进度、状态和服务器。程序不会未经用户确认自动占用网络；用户连接任务记录中的服务器后点击“恢复下载”。

下载过程中关闭程序时，状态文件与 `.part` 已在每个成功分片后更新，因此下次启动可重新计算安全偏移。

## 14. 服务端重启为什么不需要恢复任务

下载 BLOCK 不是依赖服务端文件游标的连续流，而是 `filename + offset + request_size` 的独立读取。AVServer 重启后，只要 `media/` 中的文件仍存在且版本未变，就能接受新的 INIT，并从客户端请求的非零偏移继续。

这也是下载和上传的重要区别：上传恢复必须证明半成品归属；下载的媒体本来就是只读公开列表中的正式文件。

## 15. 放弃任务处理

用户选择任务并点击“放弃任务”后，客户端经确认删除对应 `.part` 和 `.download.json`，再从表格移除记录。服务端无任务状态，所以无需发送取消包。

该操作只处理未完成任务，不删除已经完成的正式 cache 文件，也不影响远程媒体。

## 16. 下载完成后的重命名

客户端在 `confirmed_offset == file_size` 后关闭并检查 `.part` 实际大小。随后删除已有同名正式 cache 文件，并用 `QFile::rename()` 将 `.part` 改为正式文件，最后删除任务状态。

这不是跨文件系统事务，但 `.part` 与正式文件位于同一 cache 目录，正常情况下是轻量名称切换。若改名失败，状态和 `.part` 会保留，避免误报完成。

## 17. 新增文件清单

- `AVClient/modules/network/DownloadTaskStore.h`
- `AVClient/modules/network/DownloadTaskStore.cpp`
- `tools/resumable_download_test.py`
- `docs/stage_logs/STAGE9_RESUMABLE_DOWNLOAD.md`

## 18. 修改文件清单

- `AVClient/AVClient.pro`
- `AVClient/modules/network/av_protocol.h`
- `AVClient/modules/network/AVNetworkClient.h`
- `AVClient/modules/network/AVNetworkClient.cpp`
- `AVClient/pages/RemoteMediaPage.h`
- `AVClient/pages/RemoteMediaPage.cpp`
- `AVClient/pages/SettingsPage.cpp`
- `AVServer/include/av_protocol.h`
- `AVServer/include/DownloadManager.h`
- `AVServer/src/DownloadManager.cpp`
- `AVServer/src/ProtocolDispatcher.cpp`
- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/PROTOCOL_DESIGN.md`
- `docs/BUILD_AND_RUN.md`
- `docs/TEST_WORKFLOW.md`
- `docs/INTERVIEW_QA.md`

原始 `MediaPlayer`、`VideoRecorder`、`NetDisk-Client`、`NetDisk-Server` 未修改。

## 19. 构建步骤

Windows 客户端：

```powershell
cd D:\colin\project\AVProject\AVClient\build-debug
D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe ..\AVClient.pro -spec win32-g++ "CONFIG+=debug"
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe clean
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe -j4
```

Ubuntu 服务端：

```bash
cd ~/AVProject/AVServer
make clean
make
./AVServer 8000
```

## 20. 自动化测试

在能连接 AVServer 的机器上运行：

```bash
cd ~/AVProject
python3 tools/resumable_download_test.py 192.168.44.130 8000 test.mp4 \
  --blocks-before-disconnect 4 --output-dir ./download-test
```

脚本请求媒体、下载若干块、保存 `.part` 和 JSON、主动断线、以非零 offset 重连、完成下载并验证最终大小。脚本只使用 Python 标准库，不负责播放文件。

## 21. 四类人工测试

1. **网络断开恢复**：下载大文件到 20% 至 50%，断开连接，确认 `.part` 和状态保留；重连并恢复，确认从非零进度继续并可播放。
2. **客户端重启恢复**：下载中关闭 AVClient，重启后确认未完成任务出现；连接原服务器并恢复。
3. **服务端重启恢复**：下载中停止 AVServer，重启后客户端重连并恢复；确认服务端没有下载任务元数据也能继续。
4. **远程文件变化**：下载部分后替换同名远程文件，再恢复；服务端返回 `remote file changed`，客户端不向旧 `.part` 追加新数据。

回归测试还应覆盖 Ping、媒体列表、普通上传、上传续传、普通下载以及下载后播放。

## 22. 常见问题

### 恢复按钮提示服务器不匹配

任务保存了创建时的 IP 和端口。请在 Settings 连接完全相同的地址，避免将状态发往错误服务器。

### 提示 `.part` 不存在

状态文件仍在但本地半成品已被人工删除。该任务无法续传，可放弃后重新下载。

### 服务端返回 `remote file changed`

远程文件大小或修改时间与初次下载时不同。不要继续拼接；放弃旧任务，再从 0 创建新下载。

### 恢复进度比断线前略小

状态偏移和 `.part` 大小不一致时取较小值，这是防止跳过缺失数据的预期行为。

### 完成后没有自动播放

只有通过“Download and play”创建的任务其 `play_after_download` 为 true；普通 Download 恢复完成后只生成 cache 文件。

## 23. 当前未实现内容

- SHA-256 或分片哈希；
- 多任务并行下载；
- 通用暂停、取消或服务端取消协议；
- 自动重连、自动恢复和无限重试；
- 下载 token、用户权限和 TLS；
- 后台线程池与异步磁盘 I/O；
- ETag、稳定文件版本 ID 和数据库任务表；
- 在线边下边播。

## 24. 下一步建议

下一阶段更适合先补传输完整性与协议基础：为协议加入版本、统一字节序、请求 ID 和结构化错误码；然后引入有界工作线程池，在工作线程计算 SHA-256，并把完成事件回投 Reactor。若继续扩展任务体验，再抽象统一的上传/下载任务状态机与显式取消协议。

## 25. 面试问答

### Q1：什么是下载断点续传？

下载断点续传是在传输中断后保留已可靠落盘的数据，重新连接时从已完成偏移继续读取，而不是从 0 重新下载。核心不是简单 seek，而是确认本地半成品可信、远端文件版本未变，并让双方对恢复起点达成一致。

### Q2：为什么下载服务端可以无状态？

因为正式源文件已经存在，每次请求都带文件名、offset 和 request size。服务端可独立 open、seek、read，不依赖上一次请求留下的游标。状态主要位于拥有半成品的客户端。

### Q3：上传续传和下载续传有什么区别？

上传半成品在服务端，必须持久化任务身份、token、偏移并防止其他连接接管；下载半成品在客户端，服务端只需提供稳定源文件和按 offset 读取，客户端负责本地恢复状态。

### Q4：断点偏移由谁确定？

客户端先用状态和 `.part` 得到 safe offset，服务端再校验远程版本与范围并返回 accepted offset。最终恢复起点以服务端接受值为准，客户端必须截断并 seek 到该位置。

### Q5：为什么不能只相信本地状态文件？

状态可能在文件数据写入前后崩溃、被人工修改或损坏。它只代表上次记录，不一定代表磁盘真正拥有的连续字节，所以必须和 `.part` 实际大小交叉验证。

### Q6：为什么要比较 `.part` 实际大小？

若实际文件更小，按状态偏移继续会跳过缺失数据；若实际文件更大，尾部可能尚未被状态确认。取较小值并截断多余尾部，能保持 `[0, safe_offset)` 连续可信。

### Q7：safe offset 如何计算？

基本公式是 `min(confirmed_offset, part_file_size)`，同时要求两者非负且不超过总大小。实际大于确认值时截断，实际小于确认值时回写状态。

### Q8：accepted offset 有什么作用？

它表示服务端在重新检查文件版本和偏移范围后同意的恢复起点。即使当前实现通常等于请求值，显式返回也避免客户端自说自话，并为未来对齐或版本策略留出空间。

### Q9：如何判断服务端文件是否变化？

初次 INIT 保存 size 和 mtime，恢复 INIT 把旧值带回。服务端重新 stat 并精确比较；任一不同就拒绝恢复，而不是静默从 0 开始或继续拼接。

### Q10：文件大小和修改时间校验有什么不足？

它无法识别同大小且 mtime 相同的内容替换，也不能证明每个字节正确。它是低成本版本检查，不是强完整性保证；强校验应使用 SHA-256 或稳定 ETag。

### Q11：为什么下载不需要 resume token？

当前媒体列表中的正式文件本来就允许已连接客户端读取，下载请求不修改服务端文件，也不接管服务器半成品。因此没有服务端下载任务需要 token 保护。未来加入私有媒体和权限后，应通过用户鉴权控制读取，而不是简单复用上传 token。

### Q12：服务端重启后为什么还能续传？

下载服务端没有会话任务，所有必要恢复信息在客户端状态和正式媒体文件中。重启后重新 stat、INIT 和按 offset 读即可；前提是远程文件版本没有变化。

### Q13：为什么下载完成前使用 `.part`？

它隔离半成品，防止播放器或用户把不完整文件当成正常媒体。它还允许断线后保留已下载字节，并与正式 cache 文件清晰区分。

### Q14：为什么完成后再重命名？

重命名是明确提交点：只有总大小正确的半成品才获得正式文件名。这样下载失败不会留下“名字正常但内容残缺”的文件。

### Q15：断线时客户端具体做什么？

停止请求下一块、关闭当前 QFile、保留 `.part` 和状态文件，把任务标记为等待恢复。程序不自动无限重连，用户连接正确服务器后手动恢复。

### Q16：为什么一次只允许一个活动下载？

当前页面只维护一组文件句柄、offset 和响应状态。串行限制让状态机、UI 和错误处理容易验证，符合学习项目的最小范围；并行需要独立任务对象和 request ID。

### Q17：放弃下载为什么不通知服务端？

服务端没有下载任务、临时文件或锁，停止发送 BLOCK 就已经停止消耗服务端资源。客户端只需删除自己的状态和 `.part`。

### Q18：后续如何加入 SHA-256？

服务端媒体元数据提供 SHA-256，客户端完成下载后计算并比较。大文件哈希应放入工作线程，不能长时间阻塞 epoll Reactor 或 Qt UI；若需要更细粒度恢复，可再增加分片 hash。
