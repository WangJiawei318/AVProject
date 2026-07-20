# AVProject 通信协议设计

## 1. 协议解决什么问题

AVClient 和 AVServer 是两个独立进程，并且运行在不同操作系统上。它们需要约定：

- 一条消息从哪里开始、在哪里结束；
- 当前消息是 Ping、列表、上传还是下载；
- 文件名、大小、offset 和二进制数据如何排列；
- 收到非法长度、非法文件名或错误状态时如何处理。

TCP 只负责可靠传输字节，不知道“业务消息”的概念。因此项目在 TCP 之上定义了自己的应用层协议。

## 2. 为什么使用 TCP

当前业务包含文件列表和文件传输，优先要求：

- 数据不丢失；
- 数据不重复；
- 数据按发送顺序到达；
- 连接状态明确；
- Windows 和 Linux 都有成熟 socket API。

TCP 提供可靠、有序的字节流，适合控制消息和文件传输。UDP 若要达到相同可靠性，需要自行实现重传、排序、拥塞和流量控制，超出本项目当前范围。

TCP 的代价是：

- 没有天然消息边界；
- 连接需要建立和维护；
- 丢包时可能出现队头阻塞；
- 不适合直接等同于实时低延迟媒体协议。

## 3. 为什么需要自定义应用层协议

假设客户端连续发送：

```text
PING_RQ
MEDIA_LIST_RQ
DOWNLOAD_INIT_RQ
```

服务端调用 `recv()` 时，可能一次只收到半个 PING，也可能一次收到 PING 和 MEDIA_LIST 的全部加上 DOWNLOAD 的一部分。若没有长度和类型，服务端无法可靠拆分。

应用层协议提供两层信息：

```text
长度层：这一帧有多少字节
业务层：这一帧是什么类型、有哪些字段
```

## 4. 总体帧格式

线上每条消息都使用：

```text
+----------------------+-----------------------------+
| int32_t packLen      | packLen 字节业务协议包体    |
+----------------------+-----------------------------+
| 4 bytes              | variable                    |
+----------------------+-----------------------------+
```

`packLen` 只表示业务包体长度，不包含自身 4 字节。

项目当前限制：

```text
sizeof(PackType) <= packLen <= 256 KB（阶段 7 服务端）
```

客户端由 `TcpClient::sendPacket()` 添加长度头；服务端由 `EpollServer::queueResponse()` 添加长度头并进入每连接发送队列。客户端接收侧仍使用原有上限，服务端阶段 7 使用更严格的 256 KB 上限，当前 64 KB 分片协议完全处于该范围内。

## 5. TCP 粘包与半包

### 5.1 半包

发送端发出 1000 字节，接收端第一次 `recv()` 可能只得到 300 字节，剩余 700 字节以后到达。这不是 TCP 出错，而是字节流的正常行为。

项目使用两种等价的组帧方式：

- 客户端 `TcpClient::recvAll()`；
- 服务端 `ConnectionContext::receiveBuffer` + `EpollServer::parseFrames()`。

客户端循环读取指定长度；服务端 non-blocking recv 到 `EAGAIN`，把字节追加到独立缓冲，不足一帧时保留到下一次 EPOLLIN。

### 5.2 粘包

发送端连续发出两条消息，接收端一次 `recv()` 可能同时得到两条消息的字节。如果接收方按“recv 一次就是一条消息”处理，就会把两个包混在一起。

项目先读固定 4 字节长度，再按长度读包体。处理完一帧后，下一次循环重新读下一条长度，因此可以正确拆分连续消息。

## 6. 通用数据定义

协议头位于：

```text
AVClient/modules/network/av_protocol.h
AVServer/include/av_protocol.h
```

两份文件必须保持一致。

主要常量：

| 常量 | 值 | 用途 |
| --- | ---: | --- |
| `AV_NAME_SIZE` | 32 | 测试登录用户名和密码 |
| `AV_TEXT_SIZE` | 128 | 响应消息 |
| `AV_FILE_NAME_SIZE` | 256 | UTF-8 文件名缓冲 |
| `AV_EXTENSION_SIZE` | 16 | 扩展名 |
| `AV_TRANSFER_ID_SIZE` | 96 | 可恢复上传任务 ID |
| `AV_RESUME_TOKEN_SIZE` | 128 | 上传恢复凭据缓冲区 |
| `AV_UPLOAD_BLOCK_SIZE` | 65536 | 单个上传/下载分片上限 |

结构体使用：

```cpp
#pragma pack(push, 1)
```

消除编译器字段填充，保证 Windows 和 Ubuntu 当前 x86 环境中的结构大小一致。

> 当前整数直接使用主机字节序，没有调用 `htonl/ntohl`，也没有独立序列化层。Windows x86 与 Ubuntu x86 都是小端，因此当前环境可用；若跨大端架构、不同 ABI 或长期演进，应改成明确的网络字节序和字段级序列化。

## 7. 协议类型总表

| 数值 | 协议 | 方向 | 作用 |
| ---: | --- | --- | --- |
| 20001 | `PING_RQ` | Client -> Server | 测试连接 |
| 20002 | `PING_RS` | Server -> Client | 返回 Pong |
| 20003 | `LOGIN_RQ` | Client -> Server | 早期协议测试预留 |
| 20004 | `LOGIN_RS` | Server -> Client | 返回测试登录结果 |
| 20005 | `MEDIA_LIST_RQ` | Client -> Server | 请求媒体列表 |
| 20006 | `MEDIA_LIST_RS` | Server -> Client | 返回文本列表 |
| 20007 | `UPLOAD_INIT_RQ` | Client -> Server | 初始化上传 |
| 20008 | `UPLOAD_INIT_RS` | Server -> Client | 返回任务 ID、token 和初始偏移 |
| 20009 | `UPLOAD_BLOCK_RQ` | Client -> Server | 上传一个动态分片 |
| 20010 | `UPLOAD_BLOCK_RS` | Server -> Client | 确认已写 offset |
| 20011 | `UPLOAD_FINISH_RQ` | Client -> Server | 提交上传 |
| 20012 | `UPLOAD_FINISH_RS` | Server -> Client | 返回保存结果 |
| 20013 | `DOWNLOAD_INIT_RQ` | Client -> Server | 初始化下载 |
| 20014 | `DOWNLOAD_INIT_RS` | Server -> Client | 返回文件信息 |
| 20015 | `DOWNLOAD_BLOCK_RQ` | Client -> Server | 请求一个分片 |
| 20016 | `DOWNLOAD_BLOCK_RS` | Server -> Client | 返回动态分片 |
| 20017 | `DOWNLOAD_FINISH_RQ` | Client -> Server | 通知下载完成 |
| 20018 | `DOWNLOAD_FINISH_RS` | Server -> Client | 返回完成确认 |
| 20019 | `UPLOAD_RESUME_RQ` | Client -> Server | 携带任务凭据请求恢复上传 |
| 20020 | `UPLOAD_RESUME_RS` | Server -> Client | 返回服务端确认的恢复偏移 |

`LOGIN_RQ/RS` 目前不是正式用户系统，只是阶段 2 留下的协议测试结构。项目没有数据库用户认证。

## 8. Ping 协议

### 8.1 PING_RQ

```text
type: int32
message: char[128]
```

客户端发送默认文本 `ping from AVClient`。

### 8.2 PING_RS

```text
type: int32
message: char[128]
```

服务端返回 `pong from AVServer`。Ping 只能说明当前 TCP 和协议收发可用，不代表文件目录一定有权限。

## 9. 媒体列表协议

### 9.1 MEDIA_LIST_RQ

```text
type: int32
```

没有额外参数，服务端扫描固定 `media/`。

### 9.2 MEDIA_LIST_RS

固定头：

```text
type: int32
payloadSize: int32
```

后面紧跟 `payloadSize` 字节 UTF-8 文本：

```text
filename|filesize|mtime|extension\n
filename|filesize|mtime|extension\n
```

示例：

```text
demo.mp4|10485760|2026-06-27 10:20:30|mp4
record.flv|8388608|2026-06-27 10:25:10|flv
```

### 9.3 为什么列表使用文本 payload

优点：

- 人眼容易阅读日志；
- 实现简单；
- 字段数量少，便于阶段验证。

缺点：

- 依赖分隔符转义；
- 数字需要文本转换；
- 新增字段时兼容规则不明确；
- 大列表体积比二进制大；
- 没有分页。

服务端会把文件名中的 `|`、换行替换为 `_`，避免破坏行格式。

## 10. 上传协议

### 10.1 上传状态机

```text
Idle
  -> UPLOAD_INIT_RQ
WaitingInit
  -> UPLOAD_INIT_RS(success)
Transferring
  -> UPLOAD_BLOCK_RQ / UPLOAD_BLOCK_RS 循环
Finishing
  -> UPLOAD_FINISH_RQ / UPLOAD_FINISH_RS
Completed 或 Failed

WaitingResume
  -> UPLOAD_RESUME_RQ
  -> UPLOAD_RESUME_RS(success, resume_offset)
  -> Transferring
```

客户端任意时刻只有一个未确认分片。普通上传从 INIT 返回的 offset 0 开始；恢复上传从 RESUME 返回的服务端确认偏移开始。

### 10.2 UPLOAD_INIT_RQ

```text
type: int32
fileSize: int64
fileName: char[256]
extension: char[16]
```

服务端检查：

- 文件大小大于 0；
- 文件名不包含 `/`、`\` 和 `..`；
- 文件名长度合法；
- 后缀属于白名单；
- `extension` 与文件名后缀一致。

### 10.3 UPLOAD_INIT_RS

```text
type: int32
result: int32
transferId: char[96]
resumeToken: char[128]
resumeOffset: int64
finalFileName: char[256]
message: char[128]
```

成功时服务端创建：

```text
temp/<transfer_id>.part
temp/tasks/<transfer_id>.task
```

`transfer_id` 用于定位任务，`resume_token` 是恢复凭据。客户端收到成功响应后必须立即持久化两者；普通日志和 UI 不显示完整 token。`resumeOffset` 对新任务为 0。

### 10.4 UPLOAD_BLOCK_RQ

固定头：

```text
type: int32
transferId: char[96]
offset: int64
dataSize: int32
```

后面紧跟：

```text
dataSize 字节二进制文件数据
```

服务端要求：

- `0 < dataSize <= 64 KB`；
- 实际包长等于固定头加 `dataSize`；
- `transfer_id` 存在且当前连接已经绑定该任务；
- `offset` 等于服务端当前 `receivedSize`；
- 本块不越过 INIT 声明的总大小。

如果 offset 小于服务端位置且整个分片已经被确认，服务端不重复写入，只返回当前 `receivedOffset`。如果 offset 大于服务端位置，或分片与当前位置发生部分重叠，服务端返回 offset mismatch 且不写盘。

### 10.5 UPLOAD_BLOCK_RS

```text
type: int32
result: int32
transferId: char[96]
receivedOffset: int64
message: char[128]
```

客户端用服务端确认的 `receivedOffset / fileSize` 更新进度，而不是用“本地已读取字节”更新。

### 10.6 UPLOAD_FINISH_RQ

```text
type: int32
transferId: char[96]
fileName: char[256]
fileSize: int64
```

服务端比较：

- INIT 任务中的预期大小；
- BLOCK 累计接收大小；
- `.part` 的磁盘实际大小；
- FINISH 再次声明的大小。

一致后将临时文件移动到 `media/`。同名文件自动增加数字后缀。

### 10.7 UPLOAD_FINISH_RS

```text
type: int32
result: int32
fileName: char[256]
message: char[128]
```

`fileName` 是服务端最终保存名，可能与原名不同。

### 10.8 UPLOAD_RESUME_RQ

```text
type: int32
transferId: char[96]
resumeToken: char[128]
fileName: char[256]
expectedSize: int64
```

服务端同时校验任务 ID、token、原文件名和预期大小。只凭文件名或 transfer ID 不能恢复任务。若任务已经绑定其他活动连接，返回 `task already active`。

### 10.9 UPLOAD_RESUME_RS

```text
type: int32
result: int32
transferId: char[96]
resumeOffset: int64
finalFileName: char[256]
message: char[128]
```

`resumeOffset` 是服务端根据任务元数据和 `.part` 实际大小确认的安全位置，不采用客户端自报偏移。恢复成功后，客户端打开原文件并 seek 到该位置，再进入原有 BLOCK/ACK 循环。

### 10.10 服务端任务元数据

服务端为每个 uploading 任务保存一个小型键值文件，字段包括 transfer ID、token、原名、最终名、预期/已接收大小、临时路径、创建/更新时间和状态。更新流程是“写 `.tmp`、`fsync`、`rename` 覆盖”，降低崩溃留下半写元数据的概率。

启动恢复时，如果元数据 `received_size` 与 `.part` 实际大小不一致，取两者较小值并在必要时截断超前的 `.part`。这会牺牲最后少量未同步状态，但不会跳过服务端无法可靠证明已经接收的字节。

## 11. 为什么上传分成 INIT / BLOCK / FINISH

- INIT：提前拒绝非法元数据，建立任务和临时文件。
- BLOCK：固定内存传输大文件，逐块确认 offset。
- FINISH：校验完整性并提交到正式目录。

如果只使用一种“上传包”：

- 不容易区分元数据和文件数据；
- 大文件无法放进一个包；
- 中断后无法判断文件是否完整；
- 半成品可能直接暴露给媒体列表。

## 12. 下载协议

### 12.1 下载状态机

```text
Idle
  -> DOWNLOAD_INIT_RQ
WaitingInit
  -> DOWNLOAD_INIT_RS(success)
Transferring
  -> DOWNLOAD_BLOCK_RQ / DOWNLOAD_BLOCK_RS 循环
Finishing
  -> DOWNLOAD_FINISH_RQ / DOWNLOAD_FINISH_RS
Completed 或 Failed
```

### 12.2 DOWNLOAD_INIT_RQ

```text
type: int32
fileName: char[256]
resumeOffset: int64
expectedFileSize: int64
expectedModifiedTime: int64
```

新下载把后三个字段置为 0。恢复下载携带客户端计算出的安全偏移，以及第一次 INIT 保存的文件大小和修改时间。服务端不接受路径，只在固定 `media/` 中查找经过验证的普通媒体文件。

### 12.3 DOWNLOAD_INIT_RS

```text
type: int32
result: int32
fileName: char[256]
fileSize: int64
modifiedTime: int64
acceptedOffset: int64
message: char[128]
```

服务端处理规则：

- 新下载：返回当前 size、mtime，`acceptedOffset = 0`；
- 恢复下载：要求当前 size/mtime 与客户端保存值完全一致，且 `resumeOffset <= fileSize`；
- 版本一致：`acceptedOffset = resumeOffset`；
- 版本不一致：返回 `remote file changed`，不允许客户端把新旧内容拼接。

客户端用 `fileSize` 建立进度范围，并创建：

```text
AVClient/cache/<fileName>.part
```

### 12.4 DOWNLOAD_BLOCK_RQ

```text
type: int32
fileName: char[256]
offset: int64
requestSize: int32
```

客户端从 INIT 响应中的 accepted offset 开始，每次最多请求 64 KB。最后一块请求剩余字节数。

### 12.5 DOWNLOAD_BLOCK_RS

固定头：

```text
type: int32
result: int32
fileName: char[256]
offset: int64
dataSize: int32
message: char[128]
```

后面紧跟 `dataSize` 字节数据。

客户端检查：

- 响应文件名等于当前任务文件名；
- 响应 offset 等于本地当前 offset；
- `0 < dataSize <= 64 KB`；
- 本块不越过 INIT 总大小；
- 写入磁盘的返回字节数正确。

### 12.6 DOWNLOAD_FINISH_RQ

```text
type: int32
fileName: char[256]
fileSize: int64
```

服务端重新检查当前文件大小，发现下载期间文件大小变化时拒绝完成。

### 12.7 DOWNLOAD_FINISH_RS

```text
type: int32
result: int32
fileName: char[256]
message: char[128]
```

客户端成功后检查 `.part` 实际大小，将它改名为正式 cache 文件。用户选择“Download and play”时，再通知 MainWindow 调用播放器。

## 13. 为什么下载分成 INIT / BLOCK / FINISH

- INIT：先确认文件存在并得到可信大小。
- BLOCK：让客户端控制 offset 和接收节奏，天然形成背压。
- FINISH：复核服务端文件未明显变化，并记录完成边界。

这比服务端无限推送更容易控制客户端磁盘写入和 UI 状态。

## 14. 为什么采用 64 KB 分片

64 KB 的考虑：

- 远小于阶段 7 服务端 256 KB 单包上限；
- 单次内存固定；
- 比几 KB 小块减少往返次数；
- 进度反馈足够细；
- 局域网演示速度可接受。

它不是理论最优值。高延迟网络中，严格“一块一确认”的吞吐受 RTT 限制。后续可以使用滑动窗口，例如允许 4 到 16 个在途块。

## 15. 文本 payload 与二进制 payload

| 维度 | 文本 | 二进制 |
| --- | --- | --- |
| 可读性 | 高 | 低 |
| 解析难度 | 简单场景低 | 需要严格结构 |
| 空间 | 通常较大 | 紧凑 |
| 文件数据 | 不适合 | 适合 |
| 字段演进 | 需设计转义/版本 | 需设计兼容结构 |
| 当前用途 | 媒体列表 | 控制包和文件块 |

项目采用混合方式：列表使用 UTF-8 文本，文件块使用二进制。

## 16. 安全与健壮性检查

当前已有：

- 服务端 256 KB 帧长度上限；
- 64 KB 文件块上限；
- 固定结构大小检查；
- 动态头声明长度与实际包长一致性检查；
- 文件名长度检查；
- 路径分隔符和 `..` 检查；
- 媒体后缀白名单；
- 只访问普通文件；
- offset 连续性和总大小边界检查；
- 上传 temp、下载 `.part` 隔离半成品。
- 上传恢复同时校验 transfer ID、token、文件名和预期大小；
- 同一上传任务的活动连接排他绑定；
- 服务端重启时按元数据和 `.part` 实际大小确定安全偏移；
- 72 小时上传任务过期清理。
- 下载恢复时校验远程文件大小、修改时间和 resume offset；
- 客户端按状态 offset 与 `.part` 实际大小的较小值恢复。

当前没有：

- TLS 加密；
- 用户认证与授权；
- 防重放；
- 限速和配额；
- 内容哈希；
- 符号链接级别的强化防护；
- 恶意客户端连接和超时治理。

## 17. 当前协议不足

1. 客户端和服务端各维护一份协议头，存在手工同步风险。
2. 直接传 packed struct，依赖字段布局和整数表示。
3. 使用主机字节序，不是标准网络字节序。
4. 没有协议版本号和能力协商。
5. 没有 request_id，多个并发请求难以关联。
6. 上传有 transfer ID 和恢复 token；下载刻意保持服务端无状态，没有持久任务 ID。
7. 错误码只有 result 和字符串，无法程序化分类。
8. 列表是分隔文本，没有转义协议、分页和总数。
9. 串行 ACK 简单但高 RTT 下吞吐较低。
10. 没有哈希，等长内容损坏无法发现。
11. 上传和下载已支持手动任务恢复，但没有通用取消和自动重试。
12. 最大包长、块大小等能力没有协商。

## 18. 当前上传与下载断点续传

上传断点续传已经实现：

1. INIT 生成 transfer ID 和随机 resume token。
2. 客户端使用 JSON 文件持久化本地路径、mtime、服务器地址和确认 offset。
3. 服务端使用键值任务文件持久化元数据，并保留 `.part`。
4. 断线只解除内存中的 `active_owner_connection_id`，不删除任务。
5. RESUME 同时校验 ID、token、文件名和预期大小。
6. 服务端按实际已落盘状态返回安全 `resumeOffset`。
7. FINISH 成功后移动到 `media/`，删除服务端元数据和客户端状态。

安全边界：当前 token 来自 `/dev/urandom`，失败才回退 `std::random_device`；它能阻止只猜 transfer ID 的客户端，但未经过 TLS 保护，也没有绑定真实用户身份。当前文件一致性依赖文件名、大小、客户端 mtime、连续偏移和 `.part` 大小，不能识别“内容变化但大小和修改时间碰巧一致”的情况。

下载断点续传也已实现，但采用不同模型：

1. 客户端用 JSON 保存服务器地址、远程文件名、size/mtime、确认 offset 和播放意图，并保留 cache `.part`。
2. 恢复前取 `min(confirmedOffset, partFileSize)`；多余尾部截断，文件较小时降低状态偏移。
3. 扩展后的 DOWNLOAD_INIT 携带 safe offset 和上次文件版本。
4. 服务端重新 stat 正式媒体，版本一致时返回 accepted offset；版本变化时明确返回 `remote file changed`。
5. 服务端不创建下载任务、download ID、token 或持久化元数据；AVServer 重启后可直接按 offset 恢复。
6. 每块成功写入后客户端原子更新状态，完成并改名后删除状态。

下载使用 size/mtime 只是一种低成本版本判断。它不能发现“内容已变化，但大小和修改时间恰好相同”的情况，也没有传输后 SHA-256 校验。当前上传 token 也只是一种任务恢复凭据，不是真实用户权限。

## 19. 如何扩展 MD5/SHA-256 校验

简单方案：

- INIT 携带整个文件哈希；
- FINISH 时接收端计算本地文件哈希；
- 相同才提交正式文件。

更强的断点方案：

- 整体哈希确认最终完整性；
- 每块哈希确认分片内容；
- 服务端保存分块 bitmap 和哈希；
- 只重传损坏或缺失块。

MD5 适合演示内容一致性，但存在碰撞问题。需要安全语义时优先 SHA-256。

## 20. 如何扩展多任务传输

所有请求和响应都需要明确：

```text
request_id / task_id
file_id
block_index 或 offset
```

客户端维护任务表，UI 为每个任务保存状态和进度。发送层需要线程安全队列，服务端需要并发安全的任务管理器。为避免磁盘和网络被单个用户占满，还应加入：

- 最大并发任务数；
- 每用户限速和配额；
- 发送窗口；
- 超时和取消；
- 任务优先级；
- 失败重试策略。

## 21. 阶段 10：协议调度与线程池过载处理

阶段 10 **没有改变线上帧格式和业务结构体**。客户端仍然发送“4 字节包长度 + 协议包体”，阶段 1 至 9 的客户端不需要因为服务端加入线程池而修改。`connectionId`、`businessTaskInFlight`、`completionQueue` 和 `eventfd` 都是 AVServer 进程内部的调度机制，不在线上传输，也不是用户身份或恢复凭证。

服务端收到完整帧后按协议类型分流：

```text
PING_RQ
  -> Reactor 直接生成 PING_RS

MEDIA_LIST_RQ / UPLOAD_* / DOWNLOAD_*
  -> 有界业务线程池
  -> 生成一个或多个协议响应
  -> completionQueue
  -> eventfd 唤醒 Reactor
  -> Reactor 写入对应连接的 sendQueue
```

每个连接最多只有一个业务任务在线程池中执行。后续完整帧可以继续被 `recv()` 收入该连接的 `receiveBuffer`，但要等当前任务完成后再分发。因此同一连接上的上传分片、下载请求和列表请求仍按接收顺序执行；不同连接可以由不同 worker 并行处理。

线程池队列达到上限时，`submit()` 返回失败。服务端清除该连接的 in-flight 状态，并构造与原请求类型匹配的失败响应，消息为 `server busy`，而不是让连接永久停住。当前媒体列表响应没有统一的 `result/message` 字段，所以列表过载错误仍以文本 payload 返回；这是现有协议错误模型不统一的一个限制，后续可通过通用错误包或统一响应头改进。

工作线程只接收协议包副本、`connectionId` 和仅供日志使用的 fd，不持有 `ConnectionContext*`，也不直接调用 `send()` 或 `epoll_ctl()`。完成结果回投后，Reactor 必须再次按 `connectionId` 验证连接是否存在；若客户端已经断开，即使旧 fd 被操作系统复用，结果也会被丢弃，不会发送给新连接。

阶段 10 后协议层仍有以下边界：

- packed struct 和整数仍依赖当前小端环境，尚无协议版本与网络字节序转换；
- 没有统一错误码、request ID、用户鉴权或 TLS；
- `server busy` 是即时拒绝，不包含客户端退避时间，也不会自动重试；
- 有界线程池改善了 Reactor 被同步文件 I/O 阻塞的问题，但不等于已经达到生产级高并发能力。
