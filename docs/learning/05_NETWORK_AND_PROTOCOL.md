# 网络模块与协议

## 1. 两层客户端网络封装

### TcpClient

文件：`AVClient/modules/network/TcpClient.cpp`

职责是 Winsock 与字节流：构造时 `WSAStartup()`；`connectToServer()` 用非阻塞 connect + `select()` 实现超时，成功后恢复阻塞模式并启动 `std::thread recvLoop`；`sendPacket()` 加 4 字节长度头；析构/断开时关闭 socket、停止并 join 接收线程、最终 `WSACleanup()`。

`sendAll()`/`recvAll()` 循环处理短发送、短接收和中断。接收线程每次先读满 `int32_t packetLength`，校验上限，再读满包体并发 `packetReceived(QByteArray)`。

### AVNetworkClient

文件：`AVClient/modules/network/AVNetworkClient.cpp`

职责是业务协议：`sendPing()` 等方法构造 packed struct 或“header + 二进制数据”；`onPacketReceived()` 根据首个 `PackType` 校验结构体长度、解析动态 payload，再发出业务 Qt signal。UI 不直接接触 socket。

## 2. Qt 信号与线程

`recvLoop()` 在 std::thread 中发信号，接收对象属于 Qt 主线程时，AutoConnection 会以 queued 方式在对象线程执行 slot。因此接收线程不直接修改 QWidget。关闭时要先结束 socket 阻塞并 join，避免对象销毁后线程仍发信号。

## 3. 长度帧、粘包与半包

线格式：

```text
+----------------------+---------------------------+
| int32 packet_length  | protocol body             |
| 4 bytes              | packet_length bytes       |
+----------------------+---------------------------+
```

TCP 是有序字节流，没有消息边界。一次 `send` 可能被对端多次 `recv`（半包），多次 `send` 也可能被一次 `recv` 合并（粘包）。长度头使接收方先知道一个完整包体需要多少字节。客户端用 `recvAll()`；服务端用每连接 `receiveBuffer` 增量解析。

## 4. 协议编号

`DEF_PACK_BASE=20000`，按 RQ/RS 成对编号：PING 20001/2、LOGIN 20003/4、MEDIA_LIST 20005/6、UPLOAD_INIT 20007/8、UPLOAD_BLOCK 20009/10、UPLOAD_FINISH 20011/12、DOWNLOAD_INIT 20013/14、DOWNLOAD_BLOCK 20015/16、DOWNLOAD_FINISH 20017/18、UPLOAD_RESUME 20019/20。LOGIN 是早期协议测试接口，当前主业务 UI 不依赖真实登录。

## 5. 协议字段与处理

### PING_RQ / PING_RS

- RQ：`type, message[128]`；RS：`type, message[128]`。
- 客户端 `sendPing()`，服务端 `handlePing()`，客户端发 `pingResponse`。
- 失败：未连接、发送失败、包体尺寸错误或连接断开。

### LOGIN_RQ / LOGIN_RS

- RQ：`type, username[32], password[32]`；RS：`type, result, message[128]`。
- 服务端当前无用户校验，固定返回协议测试成功；不能称为用户鉴权。

### MEDIA_LIST_RQ / MEDIA_LIST_RS

- RQ：仅 `type`。
- RS header：`type, payloadSize`，后接 UTF-8 文本，每行 `name|size|mtime-text|extension\n`。
- 服务端 `MediaManager::buildMediaListPayload()` 扫描普通且支持扩展名的文件，响应最大约 256 KB，超出会截断。
- 失败：目录不可访问、队列满返回错误文本、payload 长度不合法。

### UPLOAD_INIT_RQ / RS

- RQ：`type, fileSize, fileName[256], extension[16]`。
- RS：`type, result, transferId[96], resumeToken[128], resumeOffset, finalFileName[256], message[128]`。
- 用途：创建 `.part` 与 `.task`，绑定当前 connectionId，客户端收到成功后立即保存 `.upload.json`。
- 失败：文件名不安全、扩展不支持、大小非正、目录/持久化创建失败。

### UPLOAD_RESUME_RQ / RS

- RQ：`type, transferId, resumeToken, fileName, expectedSize`。
- RS：`type, result, transferId, resumeOffset, finalFileName, message`。
- 用途：同时校验任务 ID、token、文件名、大小、状态、占用者和 `.part`，返回服务端权威偏移。
- 失败：`task not found`、`token invalid`、`file metadata mismatch`、`task expired`、`task already active`。

### UPLOAD_BLOCK_RQ / RS

- RQ header：`type, transferId, offset, dataSize`，后接最多 64 KB 二进制。
- RS：`type, result, transferId, receivedOffset, message`。
- 用途：串行追加；只有绑定 connectionId 可写。重复且已完整确认的块返回成功和当前偏移，跳跃块拒绝。
- 失败：长度不符、dataSize 越界、任务/绑定错误、offset mismatch、磁盘写入/fsync/元数据持久化失败。

### UPLOAD_FINISH_RQ / RS

- RQ：`type, transferId, fileName, fileSize`。
- RS：`type, result, fileName, message`。
- 用途：核对声明大小、receivedSize 与 `.part` 实际大小，将文件 rename 到 `media/`，删除 `.task`。
- 失败：身份/元数据不匹配、未传完、rename 失败。

### DOWNLOAD_INIT_RQ / RS

- RQ：`type, fileName, resumeOffset, expectedFileSize, expectedModifiedTime`。
- RS：`type, result, fileName, fileSize, modifiedTime, acceptedOffset, message`。
- 新下载传后三项 0；恢复时带本地安全偏移和上次服务端元数据。
- 失败：文件名/扩展不安全、文件不存在/为空、偏移越界、恢复元数据非法、`remote file changed`。

### DOWNLOAD_BLOCK_RQ / RS

- RQ：`type, fileName, offset, requestSize`。
- RS header：`type, result, fileName, offset, dataSize, message`，后接二进制。
- 每次独立打开文件、seek、最多读取 64 KB；服务端不保存下载会话。
- 失败：offset/requestSize 越界、文件变化为不可读、打开/seek/read 失败。

### DOWNLOAD_FINISH_RQ / RS

- RQ：`type, fileName, fileSize`；RS：`type, result, fileName, message`。
- 服务端只确认当前远程文件大小等于声明值；客户端负责 `.part` 大小检查、rename 和删除状态。

## 6. 动态 payload

媒体列表使用“固定 header + 文本”；上传块和下载块使用“固定 header + 原始二进制”。这样不需要把 64 KB 固定数组放进每个结构体，也避免二进制经过文本编码膨胀。接收方必须同时验证 header、声明 dataSize 和实际包长。

## 7. 字节序与结构体边界

当前 `av_protocol.h` 使用 `#pragma pack(push, 1)` 消除填充，并直接 `memcpy` C++ 结构体；长度、type、整数均为主机字节序，没有 `htonl/ntohl`，`int64_t` 也没有显式网络序转换。当前双方通常都是小端 x86-64，能工作，但存在：

- 大端主机不兼容；不同编译器 ABI/类型约定需要持续核对。
- 固定数组浪费部分带宽，协议演进缺少 version/capability 字段。
- 文本字段依赖 NUL 截断与 UTF-8 容量，超长名称会被拒绝或复制失败。
- 没有 TLS、MAC、消息级认证、request ID 与校验和。

生产化应显式序列化每个整数为网络字节序，定义版本和错误码，或采用成熟 schema；不能简单移除 pack 而保持裸结构体传输。

## 8. 断线行为

`TcpClient` 收到 EOF/错误后关闭 socket 并发 disconnected/error。`RemoteMediaPage::slotConnectedChanged(false)` 停止继续发块、关闭本地文件、把活动任务保存为等待恢复。上传服务端解除 `activeOwnerConnectionId` 但保留任务；下载服务端无状态，无需解绑。

