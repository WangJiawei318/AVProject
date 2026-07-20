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
0 < packLen <= 1 MB
```

客户端由 `TcpClient::sendPacket()` 添加长度头，服务端由 `AVServer::sendPacket()` 添加长度头。

## 5. TCP 粘包与半包

### 5.1 半包

发送端发出 1000 字节，接收端第一次 `recv()` 可能只得到 300 字节，剩余 700 字节以后到达。这不是 TCP 出错，而是字节流的正常行为。

项目使用：

- 客户端 `TcpClient::recvAll()`；
- 服务端 `AVServer::readExact()`。

它们循环调用 `recv()`，直到读满目标长度或连接断开。

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
| `AV_UPLOAD_ID_SIZE` | 64 | 上传任务 ID |
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
| 20008 | `UPLOAD_INIT_RS` | Server -> Client | 返回 upload_id |
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
```

客户端任意时刻只有一个未确认分片。

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
uploadId: char[64]
message: char[128]
```

成功时服务端创建：

```text
temp/<upload_id>.part
```

`upload_id` 用于后续 BLOCK 和 FINISH 找到对应任务。

### 10.4 UPLOAD_BLOCK_RQ

固定头：

```text
type: int32
uploadId: char[64]
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
- `upload_id` 存在；
- `offset` 等于服务端当前 `receivedSize`；
- 本块不越过 INIT 声明的总大小。

### 10.5 UPLOAD_BLOCK_RS

```text
type: int32
result: int32
uploadId: char[64]
receivedOffset: int64
message: char[128]
```

客户端用服务端确认的 `receivedOffset / fileSize` 更新进度，而不是用“本地已读取字节”更新。

### 10.6 UPLOAD_FINISH_RQ

```text
type: int32
uploadId: char[64]
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
```

服务端不接受路径，只在固定 `media/` 中查找经过验证的普通媒体文件。

### 12.3 DOWNLOAD_INIT_RS

```text
type: int32
result: int32
fileName: char[256]
fileSize: int64
message: char[128]
```

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

客户端从 offset 0 开始，每次最多请求 64 KB。最后一块请求剩余字节数。

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

- 远小于 1 MB 单包上限；
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

- 1 MB 帧长度上限；
- 64 KB 文件块上限；
- 固定结构大小检查；
- 动态头声明长度与实际包长一致性检查；
- 文件名长度检查；
- 路径分隔符和 `..` 检查；
- 媒体后缀白名单；
- 只访问普通文件；
- offset 连续性和总大小边界检查；
- 上传 temp、下载 `.part` 隔离半成品。

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
6. 上传有 upload_id，下载没有 download_id。
7. 错误码只有 result 和字符串，无法程序化分类。
8. 列表是分隔文本，没有转义协议、分页和总数。
9. 串行 ACK 简单但高 RTT 下吞吐较低。
10. 没有哈希，等长内容损坏无法发现。
11. 没有超时、取消、重试和任务恢复。
12. 最大包长、块大小等能力没有协商。

## 18. 如何扩展断点续传

需要增加：

```text
任务 ID
文件稳定标识：大小 + mtime + hash
客户端已持久化 offset
服务端任务元数据持久化
RESUME_QUERY_RQ / RESUME_QUERY_RS
任务超时与过期清理
```

上传恢复：

1. 客户端重新提交文件标识。
2. 服务端查找 `.part` 和任务记录。
3. 返回安全恢复 offset。
4. 客户端从该 offset 继续。

下载恢复：

1. 客户端保留 `.part` 和元数据。
2. INIT 携带本地大小和服务端文件版本。
3. 服务端确认同一文件版本后返回恢复位置。
4. 若版本不同，删除旧 `.part` 从零开始。

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

