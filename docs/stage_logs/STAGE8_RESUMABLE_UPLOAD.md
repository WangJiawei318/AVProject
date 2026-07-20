# 阶段 8：上传断点续传与任务恢复

## 1. 阶段目标

本阶段在阶段 7 的 epoll LT 单线程 Reactor 上增加上传断点续传，完成以下闭环：

```text
创建上传任务
  -> 服务端持久化任务和 .part
  -> 客户端持久化源文件与恢复凭据
  -> 上传若干 64 KB 分片
  -> 网络断开、AVClient 退出或 AVServer 重启
  -> 双方重新加载任务
  -> 客户端携带 transfer_id + resume_token 请求恢复
  -> 服务端返回安全 resume_offset
  -> 客户端从该位置继续
  -> FINISH 校验大小并提交到 media/
```

本阶段不实现下载断点续传、线程池、多文件并行上传、通用任务取消、用户系统、MySQL、边下边播或完整 SHA-256 校验。

## 2. 为什么先做上传断点续传

上传的源文件在客户端，半成品在服务端。大文件上传中断后，如果服务端直接删除 `.part`，用户只能从零开始，既浪费时间也浪费网络流量。现有 INIT/BLOCK/FINISH 和连续 offset 已经提供清晰的任务边界，适合先增加恢复能力。

下载恢复需要额外处理服务端源文件版本、客户端 cache `.part` 生命周期和下载任务身份。把上传与下载分成两个阶段，可以控制修改范围，也方便分别验证。

## 3. 断点续传完整流程

新上传流程：

1. 客户端选择文件，记录绝对路径、大小和最后修改时间。
2. 客户端发送 `UPLOAD_INIT_RQ`。
3. 服务端生成 transfer ID 和 resume token，创建 `.part` 与任务元数据。
4. 服务端返回扩展后的 `UPLOAD_INIT_RS`。
5. 客户端立即保存本地任务状态，再发送第一块。
6. 每块在服务端 `pwrite + fsync` 成功后更新 `received_size` 和任务文件。
7. 客户端每收到一个 BLOCK ACK 就更新 `confirmed_offset`。

恢复流程：

1. 客户端重连原服务器，用户点击“恢复上传”。
2. 客户端检查源文件仍存在，大小和 mtime 未变化。
3. 客户端发送 `UPLOAD_RESUME_RQ`。
4. 服务端校验任务、token、文件名、大小、状态和活动连接。
5. 服务端核对 `.part` 实际大小，返回安全 `resume_offset`。
6. 客户端 seek 到该位置，继续原来的 BLOCK/ACK 循环。
7. FINISH 成功后，服务端将 `.part` 移入 `media/` 并删除任务文件；客户端删除本地状态并刷新媒体列表。

## 4. transfer_id 与 resume_token

`transfer_id` 是全局任务定位键。当前实现组合时间、进程 ID、进程内递增值和随机字节，避免不同进程或快速创建任务时发生简单碰撞。

`resume_token` 是恢复凭据。服务端优先从 `/dev/urandom` 读取 32 字节并编码为 64 个十六进制字符；读取失败时回退到 `std::random_device`。日志允许显示 transfer ID，但不打印完整 token。

安全边界必须如实说明：token 能阻止只猜到 transfer ID 的客户端直接操作任务，但当前连接没有 TLS，token 也没有绑定真实用户身份，因此它不是完整认证和授权方案。

## 5. fd 为什么不能作为永久任务身份

fd 只在一个 Linux 进程的某次连接中有效：

- 客户端断线重连后 fd 会变化；
- AVServer 重启后旧 fd 不存在；
- 内核可能把相同数字复用给完全不同的连接。

因此持久任务身份必须独立于连接，使用 `transfer_id + resume_token`。fd 只参与当前会话的排他控制。

## 6. active_owner_fd 设计

每个内存任务拥有 `activeOwnerFd`，但该值不写入元数据：

- 新建任务后绑定当前 fd；
- 恢复成功后绑定新的 fd；
- BLOCK 和 FINISH 必须来自已绑定 fd；
- 同一任务已绑定活动连接时，其他连接恢复会收到 `task already active`；
- 连接关闭时 `unbindConnection(fd)` 只解绑，不删除任务；
- 服务端重启加载任务时统一设为 `-1`。

当前 Reactor 单线程串行修改任务表，因此检查与绑定不会在两个业务线程中同时执行。以后加入线程池时必须重新设计这部分同步。

## 7. 服务端任务元数据格式

目录：

```text
AVServer/temp/tasks/<transfer_id>.task
```

键值格式：

```text
transfer_id=...
resume_token=...
original_filename=demo.mp4
final_filename=demo.mp4
expected_size=8388608
received_size=1310720
temp_path=temp/<transfer_id>.part
created_time=...
updated_time=...
status=uploading
```

任务目录权限为 0700，元数据文件和 `.part` 创建权限为 0600。当前正式持久状态主要是 `uploading`；成功任务立即删除元数据，不依赖 completed 记录进行恢复。

## 8. 原子更新任务元数据

`persistTask()` 不直接截断正式 `.task`，而是：

1. 生成完整的小型键值文本；
2. 写入同目录 `<transfer_id>.task.tmp`；
3. 对临时文件执行 `fsync`；
4. 关闭文件；
5. `rename` 覆盖正式任务文件。

同文件系统内 rename 提供原子名称切换，能显著降低进程崩溃留下半个元数据文件的风险。当前没有显式 fsync 任务目录，也没有跨 `.part` 与 `.task` 的完整事务，这属于后续可强化边界。

## 9. 服务端重启如何恢复任务

`UploadManager::initialize()` 在服务端进入事件循环前执行：

1. 创建 `temp/` 与 `temp/tasks/`；
2. 扫描后缀为 `.task` 的任务文件；
3. 解析并校验 ID、token、文件名、大小、路径和状态；
4. 检查对应 `.part` 是普通文件；
5. 修正安全偏移并把 `activeOwnerFd` 设为 `-1`；
6. 恢复到内存任务表；
7. 执行一次过期清理；
8. 扫描没有元数据的孤立 `.part` 并记录日志，不盲目恢复。

因此恢复不依赖旧进程中的 upload ID 映射或旧 fd。

## 10. 客户端本地状态持久化

客户端每个任务保存为：

```text
AVClient/transfer_state/<transfer_id>.upload.json
```

主要字段：

```text
local_file_path
filename
file_size
last_modified_ms
server_ip
server_port
transfer_id
resume_token
confirmed_offset
final_filename
status
```

`UploadTaskStore` 使用 `QSaveFile` 原子提交 JSON。AVClient 启动时加载全部未完成任务；若源文件不存在，显示不可恢复；若大小或 mtime 改变，禁止直接续传；若当前连接的 IP/端口与记录不同，提示连接对应服务器。

## 11. resume_offset 如何确定

恢复偏移的权威在服务端。设元数据偏移为 `M`，`.part` 实际大小为 `P`，安全偏移为：

```text
resume_offset = min(M, P)
```

如果 `P > M`，超出部分可能已经写入但未形成可靠元数据确认，服务端截断到 `M`。如果 `M > P`，说明元数据比文件超前，必须退回 `P`。修正后更新任务元数据并打印日志。

客户端保存的 `confirmed_offset` 只用于 UI 和恢复前参考，收到 RESUME 响应后必须服从服务端偏移。

## 12. offset 不匹配如何处理

当前任务只表示一个连续有效区间 `[0, received_size)`：

- `offset == received_size`：正常写入并确认新偏移；
- `offset < received_size` 且整块都已确认：不重复写，返回当前偏移；
- `offset < received_size` 但分片跨过当前位置：拒绝，返回 offset mismatch；
- `offset > received_size`：拒绝，不制造文件空洞；
- `received_size + data_size > expected_size`：拒绝越界块。

客户端收到服务端当前偏移后可以重新 seek 并调整发送位置。

## 13. 任务过期清理

当前 TTL 为 72 小时：

- 服务端启动时扫描一次；
- Reactor 的 `epoll_wait` 使用 60 秒超时；
- 每 5 分钟调用一次低频维护；
- 只清理 status 为 uploading、超过 TTL 且没有 `active_owner_fd` 的任务；
- 同时删除 `.part` 与 `.task`；
- 日志记录 transfer ID 和已占用字节数。

不会在每次 epoll 循环中扫描磁盘，也不会清理当前正在上传的任务。

## 14. 多客户端任务竞争

任务恢复校验顺序包括 ID、token、文件元数据、状态和当前绑定。如果 A 已成功创建或恢复任务，B 即使持有完全相同的凭据也会收到 `task already active`，直到 A 的连接关闭并触发解绑。

BLOCK/FINISH 还会再次检查当前 fd，避免未经 RESUME 绑定的连接直接使用 transfer ID 写块或提交任务。

## 15. 文件一致性边界

当前可以检查：

- transfer ID 与 resume token；
- 原文件名和预期大小；
- 客户端源文件最后修改时间；
- 服务端连续 offset；
- `.part` 实际大小；
- FINISH 声明大小、任务大小和磁盘大小。

这些检查能避免大部分误恢复，但不能识别“文件内容改变，而大小和 mtime 碰巧完全相同”的情况，也不能提供密码学完整性证明。

## 16. 为什么本阶段暂不计算完整 SHA-256

当前服务端是单线程 Reactor。对大文件在事件线程中完整扫描并计算 SHA-256，会在计算期间拖延所有连接的 Ping、列表和传输事件。

后续加入有界工作线程池后，可以在 FINISH 阶段把哈希计算提交到工作线程，完成后把“校验成功/失败”结果安全回投 Reactor，再决定是否移动到 `media/`。在此之前不能声称已经具备强内容完整性保证。

## 17. epoll Reactor 下同步元数据 I/O 的影响

每个 64 KB 分片当前执行数据写入、`fsync`、小元数据写入、`fsync` 和 rename。好处是服务端 ACK 对应的偏移具有清晰持久化含义；代价是磁盘同步频繁，会降低吞吐，并可能短暂阻塞其他连接。

本阶段优先可靠恢复。后续可以按分片数量或时间间隔批量刷新元数据，但必须重新定义 ACK 与“已可靠持久化偏移”的关系，不能为了速度返回尚未达到承诺持久化等级的偏移。

## 18. 新增文件清单

- `AVClient/modules/network/UploadTaskStore.h`
- `AVClient/modules/network/UploadTaskStore.cpp`
- `tools/resumable_upload_test.py`
- `docs/stage_logs/STAGE8_RESUMABLE_UPLOAD.md`

运行时还会生成但不会提交：

- `AVClient/transfer_state/*.upload.json`
- `AVServer/temp/*.part`
- `AVServer/temp/tasks/*.task`

## 19. 修改文件清单

协议与服务端：

- `AVServer/include/av_protocol.h`
- `AVServer/include/UploadManager.h`
- `AVServer/src/UploadManager.cpp`
- `AVServer/include/ProtocolDispatcher.h`
- `AVServer/src/ProtocolDispatcher.cpp`
- `AVServer/include/EpollServer.h`
- `AVServer/src/EpollServer.cpp`

客户端：

- `AVClient/AVClient.pro`
- `AVClient/modules/network/av_protocol.h`
- `AVClient/modules/network/AVNetworkClient.h`
- `AVClient/modules/network/AVNetworkClient.cpp`
- `AVClient/pages/RemoteMediaPage.h`
- `AVClient/pages/RemoteMediaPage.cpp`
- `AVClient/pages/SettingsPage.cpp`

工程与文档：

- `.gitignore`
- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/PROTOCOL_DESIGN.md`
- `docs/BUILD_AND_RUN.md`
- `docs/TEST_WORKFLOW.md`
- `docs/INTERVIEW_QA.md`

原始 `MediaPlayer`、`VideoRecorder`、`NetDisk-Client`、`NetDisk-Server` 未修改。

## 20. 构建方法

Windows 客户端：

```powershell
cd D:\colin\project\AVProject\AVClient\build-debug
D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe ..\AVClient.pro -spec win32-g++ "CONFIG+=debug"
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe -j4
```

Ubuntu 服务端：

```bash
cd ~/AVProject/AVServer
make clean
make
./AVServer 8000
```

Windows 客户端已在本机完整编译链接通过。Ubuntu 服务端需要把当前代码同步到虚拟机后执行上述命令确认。

## 21. 自动化测试方法

语法检查：

```bash
python3 -m py_compile tools/resumable_upload_test.py
```

连接正在运行的 AVServer：

```bash
python3 tools/resumable_upload_test.py 192.168.44.130 8000 \
  --size-mb 8 --blocks-before-disconnect 20
```

脚本会创建测试文件、INIT、把 transfer ID 和 token 写入临时 JSON、发送若干块、主动断线、重新读取状态、连接、RESUME、验证偏移大于 0、继续上传并 FINISH。若脚本能访问服务端目录，增加 `--server-media-dir` 可直接比较正式文件大小；否则按输出提示在 Ubuntu 使用 `stat` 人工核对。脚本不会打印 token，临时目录退出时自动删除。

服务端重启无法由单个普通客户端脚本安全控制，仍按下一节场景 C 人工验证。

## 22. 五类人工测试

**场景 A：网络断开恢复**

上传大文件到 20% 至 50%，点击断开；确认服务端保留 `.part` 和 `.task`。重新连接同一地址，点击恢复，确认进度不是从 0 开始并最终完成。

**场景 B：客户端重启恢复**

上传部分文件后直接关闭 AVClient。重新启动，确认未完成任务表仍显示任务；连接原服务器并恢复。

**场景 C：服务端重启恢复**

上传部分文件后停止 AVServer，再启动并观察 `restored upload task` 日志。AVClient 重连并恢复，确认使用服务端返回偏移。

**场景 D：非法恢复**

用自动化工具或临时测试客户端修改 token，发送 RESUME。服务端应返回 token invalid，原任务状态和 `.part` 不受破坏。

**场景 E：多客户端竞争**

A 先恢复任务并保持连接；B 使用相同任务凭据请求恢复。B 应收到 task already active，A 的后续 BLOCK 和 FINISH 正常。

## 23. 常见问题排查

| 现象 | 检查方法 |
| --- | --- |
| 启动后没有恢复任务 | 确认从 `AVServer/` 启动，检查 `temp/tasks/*.task` 与 `.part` 是否同时存在 |
| `task not found` | 任务可能已完成、过期或元数据无效；查看启动扫描日志 |
| `token invalid` | 确认使用 INIT 返回并由同一客户端状态文件保存的 token，不要手工改写 |
| `file metadata mismatch` | 检查本地文件名、大小；AVClient 还会检查 mtime |
| `task already active` | 另一连接仍绑定任务；关闭旧连接并等待服务端处理断开 |
| `offset mismatch` | 以响应中的服务端确认偏移重新 seek，不要使用客户端猜测值 |
| 客户端重启后列表为空 | 检查 `AVClient/transfer_state/` 是否在 exe 上一级目录且可写 |
| 元数据存在但无法加载 | 检查字段完整性、token 格式、temp_path 与 transfer ID 是否一致 |
| 只有孤立 `.part` | 服务端只记录日志，不盲目恢复；等待人工检查或过期维护策略之外清理 |
| FINISH 失败 | 比较任务 expected size、received size 与 `stat` 的 `.part` 大小 |

## 24. 当前不足

- 没有下载断点续传；
- 没有 SHA-256 强内容校验；
- token 没有绑定用户，也没有 TLS 保护；
- 客户端“放弃任务”只删除本地记录，远端依赖 72 小时过期清理；
- 每块同步元数据增加磁盘 I/O；
- 元数据是文件，不是数据库，没有事务查询和集中管理；
- 若服务端已完成 FINISH，但客户端在收到响应并删除本地状态前崩溃，重启后会留下一个收到 `task not found` 的本地记录；确认媒体已存在后需手动放弃该记录；
- 传输仍是单任务串行 ACK，没有窗口、限速和并行块；
- 没有协议版本、网络字节序、结构化错误码和 request ID。

## 25. 下一阶段下载断点续传建议

下载恢复不能简单相信本地 `.part` 长度。建议新增 download transfer ID 和服务端文件稳定标识，例如相对路径、大小、mtime 与后续 SHA-256/ETag。客户端持久化 `.part` 路径、已确认范围和服务器地址，重连后先询问服务端源文件版本；版本一致才从本地安全长度继续，版本变化则提示用户重新下载。

仍建议保持“完整下载后播放”，不要在同一阶段引入边下边播。下载恢复稳定后，再评估工作线程池与 FINISH 哈希校验。

## 26. 面试问答

### Q1：什么是断点续传？

断点续传是在传输中断后保留双方已经确认的任务状态，重新连接时从安全偏移继续，而不是从 0 重传。关键不是简单保存一个数字，而是证明该数字之前的数据属于同一个文件且已经可靠保存。

### Q2：断点偏移由谁决定？

由接收方决定。上传的接收方是服务端，所以 AVServer 根据任务元数据和 `.part` 实际大小返回 `resume_offset`；客户端必须服从该位置。

### Q3：为什么不能完全相信客户端偏移？

客户端状态可能因为崩溃、未刷盘、旧 ACK 或恶意篡改而不准确。服务端若按客户端更大的 offset 跳写，会留下空洞或缺失数据，因此必须以接收端可证明的连续落盘范围为准。

### Q4：transfer ID 和 resume token 分别解决什么问题？

transfer ID 解决“找到哪一个任务”，resume token 解决“当前调用方是否持有恢复凭据”。ID 可以出现在日志，token 不应出现在普通 UI 和日志。

### Q5：为什么 fd 不能作为任务唯一身份？

fd 只属于当前进程的一次连接，重连会变化、重启会消失，还可能被复用。因此 fd 只能做活动会话绑定，不能做跨连接和跨进程的任务身份。

### Q6：为什么客户端和服务端都要持久化状态？

客户端掌握源文件路径、mtime、服务器地址和 token；服务端掌握 `.part`、预期大小和可靠偏移。任一方缺失，都无法在双方重启后安全地重新建立任务关系。

### Q7：服务端重启后如何恢复？

启动时扫描 `temp/tasks/*.task`，验证对应 `.part`，按较小安全偏移修正任务，清空活动 fd，再装入内存映射等待 RESUME。它不依赖旧进程内存。

### Q8：`.part` 文件有什么作用？

它隔离未完成内容，避免媒体列表和播放器把半成品当正式文件；同时也是断点恢复的数据基础。只有 FINISH 成功才 rename 到 `media/`。

### Q9：元数据和 `.part` 大小不一致怎么办？

取较小值作为安全偏移。必要时截断超前的 `.part` 并更新元数据。宁可重传少量数据，也不跳过无法确认的数据。

### Q10：为什么 offset 必须严格连续？

当前只保存一个连续前缀长度。严格连续无需 bitmap 或区间集合，恢复点唯一，FINISH 检查也简单。乱序上传应作为另一套任务模型设计。

### Q11：重复分片如何处理？

如果该块完整落在已确认范围内，服务端不重复写，直接返回当前确认偏移。这样能容忍 ACK 丢失后客户端重发，又不会重复增长文件。

### Q12：跳跃分片如何处理？

offset 大于服务端当前位置时拒绝并返回 offset mismatch，不写入数据。否则会制造空洞，无法用单一 received size 表达文件完整性。

### Q13：如何避免两个客户端同时恢复同一任务？

恢复成功时设置 `active_owner_fd`。另一连接即使 token 正确也会收到 task already active；断线时再解绑。当前单线程 Reactor 保证绑定切换串行执行。

### Q14：任务为什么需要过期机制？

断线后不再立即删除 `.part`，因此永不返回的客户端会占用磁盘。TTL 在可恢复时间和存储空间之间建立边界，且活动任务不能被清理。

### Q15：为什么断开时不再立即删除 `.part`？

立即删除会让断点续传失去数据基础。阶段 8 将断线语义从“销毁任务”改为“解除当前连接绑定”，由完成或过期事件负责最终清理。

### Q16：客户端如何放弃任务？

当前“放弃任务”只删除客户端本地 JSON，避免误操作远程任务。服务端残留由 72 小时过期策略清理；未来可以新增带 token 的 ABORT 协议。

### Q17：当前为什么不能保证强内容一致性？

大小、mtime 和连续 offset 不能证明每个字节相同。内容变化但大小和 mtime 恰好一致时可能无法发现，因此需要 SHA-256 才能提供更强完整性判断。

### Q18：SHA-256 应该在哪个线程计算？

应在后续有界工作线程池中计算。大文件哈希会持续读盘和占用 CPU，不应长时间阻塞 epoll Reactor；完成结果再回投事件线程提交任务。

### Q19：上传断点续传和下载断点续传有什么区别？

上传的半成品与权威偏移在服务端，下载的半成品在客户端。下载还必须确认服务端源文件版本未变化，因此需要文件版本 ID、mtime、ETag 或 hash。

### Q20：后续如何加入用户鉴权？

登录成功后由服务端建立用户会话，上传任务元数据保存 owner user ID。RESUME 除 token 外还必须验证当前会话用户与任务所有者一致，并配合 TLS 防止 token 在链路上被窃取。

### Q21：为什么元数据更新使用临时文件加 rename？

它避免直接覆盖时崩溃留下半个任务文件。同目录临时文件写完并 fsync 后再 rename，读取者只能看到旧版本或新版本。后续还可增加目录 fsync 强化断电语义。

### Q22：为什么每个分片都更新元数据？

它让服务端 ACK 偏移与可恢复状态尽量一致，降低崩溃后的回退量。代价是较多 fsync 和 rename；本阶段优先正确性，后续才能在明确 ACK 语义后批量刷新。

### Q23：resume token 当前安全吗？

随机强度优先来自 `/dev/urandom`，比递增数字难猜，但它仍以明文随自定义 TCP 协议传输，也没有绑定用户。它是阶段性能力凭据，不是生产级认证体系。

### Q24：已完成任务为什么不能再次续传？

FINISH 成功后正式文件已经提交，服务端删除任务元数据并从内存表移除。再次 RESUME 会得到 task not found，避免重复提交或覆盖已完成媒体。

### Q25：为什么孤立 `.part` 不能自动恢复？

没有元数据就无法证明原文件名、预期大小、token 和状态。仅凭 `.part` 名称恢复可能把未知或损坏文件交给错误客户端，所以当前只记录日志，等待人工处理。

推荐提交信息：

```text
feat(upload): add resumable upload task recovery
```
